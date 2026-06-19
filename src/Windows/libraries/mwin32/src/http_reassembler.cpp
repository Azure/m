// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "http_reassembler.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace m::mwin32_impl
{
    namespace
    {
        // The HTTP line terminator and the header/body separator.
        constexpr std::string_view crlf = "\r\n";
        constexpr std::string_view header_body_separator = "\r\n\r\n";

        // The header whose value frames the body in v1 (Content-Length only).
        constexpr std::string_view content_length_name = "Content-Length";

        //
        // ASCII case-insensitive equality. Header field names are
        // case-insensitive per RFC 9110; we restrict folding to ASCII because
        // header names are tokens drawn from the US-ASCII repertoire.
        //
        bool iequals_ascii(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                unsigned char const ca =
                    static_cast<unsigned char>(a[i]);
                unsigned char const cb =
                    static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb))
                {
                    return false;
                }
            }
            return true;
        }

        //
        // Strip leading and trailing optional whitespace (space and horizontal
        // tab) as defined by the OWS rule in RFC 9110.
        //
        std::string_view trim_ows(std::string_view s)
        {
            auto const is_ows = [](char c) {
                return c == ' ' || c == '\t';
            };
            while (!s.empty() && is_ows(s.front()))
            {
                s.remove_prefix(1);
            }
            while (!s.empty() && is_ows(s.back()))
            {
                s.remove_suffix(1);
            }
            return s;
        }

        //
        // Parse a header block (the bytes preceding the terminating
        // CRLFCRLF) into a start line and a list of header fields. The block
        // is split on CRLF; the first line is the start line and each
        // remaining non-empty line is split at the first colon into a name and
        // an OWS-trimmed value. A line with no colon is malformed and skipped.
        //
        void parse_header_block(std::string_view block,
                                std::string& start_line,
                                std::vector<http_header>& headers)
        {
            start_line.clear();
            headers.clear();

            bool first = true;
            std::size_t pos = 0;
            while (pos <= block.size())
            {
                std::size_t const eol = block.find(crlf, pos);
                std::string_view line;
                if (eol == std::string_view::npos)
                {
                    line = block.substr(pos);
                    pos = block.size() + 1;
                }
                else
                {
                    line = block.substr(pos, eol - pos);
                    pos = eol + crlf.size();
                }

                if (first)
                {
                    start_line.assign(line);
                    first = false;
                    continue;
                }

                if (line.empty())
                {
                    continue;
                }

                std::size_t const colon = line.find(':');
                if (colon == std::string_view::npos)
                {
                    // Malformed header line without a colon; skip in v1.
                    continue;
                }

                http_header h;
                h.name.assign(trim_ows(line.substr(0, colon)));
                h.value.assign(trim_ows(line.substr(colon + 1)));
                headers.push_back(std::move(h));
            }
        }

        //
        // Determine the body length from the parsed headers. Returns the value
        // of the first `Content-Length` header parsed as a non-negative
        // decimal integer. A missing header, an empty value, or a value
        // containing any non-digit character yields zero (no body) in v1.
        //
        std::size_t body_length_from_headers(
            std::vector<http_header> const& headers)
        {
            for (auto const& h : headers)
            {
                if (!iequals_ascii(h.name, content_length_name))
                {
                    continue;
                }

                std::string_view const value = h.value;
                if (value.empty())
                {
                    return 0;
                }

                std::size_t result = 0;
                for (char const c : value)
                {
                    if (c < '0' || c > '9')
                    {
                        return 0;
                    }
                    result = result * 10 +
                             static_cast<std::size_t>(c - '0');
                }
                return result;
            }
            return 0;
        }

        //
        // Parse a request-line ("method SP request-target SP HTTP-version")
        // into its three space-separated fields. Missing fields are left
        // empty.
        //
        void parse_request_line(std::string_view line,
                                http_request& out)
        {
            std::size_t const first_sp = line.find(' ');
            if (first_sp == std::string_view::npos)
            {
                out.method.assign(line);
                return;
            }
            out.method.assign(line.substr(0, first_sp));

            std::size_t const second_sp =
                line.find(' ', first_sp + 1);
            if (second_sp == std::string_view::npos)
            {
                out.target.assign(line.substr(first_sp + 1));
                return;
            }
            out.target.assign(
                line.substr(first_sp + 1, second_sp - first_sp - 1));
            out.version.assign(line.substr(second_sp + 1));
        }

        //
        // Parse a status-line ("HTTP-version SP status-code SP reason-phrase")
        // into its fields. The reason phrase is optional. A non-numeric or
        // missing status code yields a status of zero.
        //
        void parse_status_line(std::string_view line,
                               http_response& out)
        {
            std::size_t const first_sp = line.find(' ');
            if (first_sp == std::string_view::npos)
            {
                out.version.assign(line);
                return;
            }
            out.version.assign(line.substr(0, first_sp));

            std::string_view const rest = line.substr(first_sp + 1);
            std::size_t const second_sp = rest.find(' ');
            std::string_view code;
            if (second_sp == std::string_view::npos)
            {
                code = rest;
            }
            else
            {
                code = rest.substr(0, second_sp);
                out.reason.assign(rest.substr(second_sp + 1));
            }

            int value = 0;
            bool any = false;
            for (char const c : code)
            {
                if (c < '0' || c > '9')
                {
                    any = false;
                    break;
                }
                any = true;
                value = value * 10 + (c - '0');
            }
            out.status_code = any ? value : 0;
        }
    } // namespace

    void http_framing::feed(void const* data, std::size_t length)
    {
        if (data == nullptr || length == 0)
        {
            return;
        }
        auto const* bytes = static_cast<std::uint8_t const*>(data);
        m_buffer.insert(m_buffer.end(), bytes, bytes + length);
    }

    bool http_framing::next(http_frame& out)
    {
        for (;;)
        {
            if (m_state == state::reading_headers)
            {
                std::string_view const view(
                    reinterpret_cast<char const*>(m_buffer.data()),
                    m_buffer.size());
                std::size_t const sep = view.find(header_body_separator);
                if (sep == std::string_view::npos)
                {
                    return false;
                }

                std::string_view const block = view.substr(0, sep);
                m_partial = http_frame{};
                parse_header_block(block, m_partial.start_line,
                                   m_partial.headers);
                m_body_remaining =
                    body_length_from_headers(m_partial.headers);

                std::size_t const consumed =
                    sep + header_body_separator.size();
                m_buffer.erase(m_buffer.begin(),
                               m_buffer.begin() +
                                   static_cast<std::ptrdiff_t>(consumed));
                m_state = state::reading_body;
            }

            // state::reading_body
            std::size_t const take =
                std::min(m_body_remaining, m_buffer.size());
            if (take > 0)
            {
                m_partial.body.insert(
                    m_partial.body.end(), m_buffer.begin(),
                    m_buffer.begin() +
                        static_cast<std::ptrdiff_t>(take));
                m_buffer.erase(m_buffer.begin(),
                               m_buffer.begin() +
                                   static_cast<std::ptrdiff_t>(take));
                m_body_remaining -= take;
            }

            if (m_body_remaining != 0)
            {
                // Need more body bytes before this message is complete.
                return false;
            }

            out = std::move(m_partial);
            m_partial = http_frame{};
            m_state = state::reading_headers;
            return true;
        }
    }

    void http_request_reassembler::feed(void const* data, std::size_t length)
    {
        m_framing.feed(data, length);
    }

    bool http_request_reassembler::next(http_request& out)
    {
        http_frame frame;
        if (!m_framing.next(frame))
        {
            return false;
        }
        out = http_request{};
        parse_request_line(frame.start_line, out);
        out.headers = std::move(frame.headers);
        out.body = std::move(frame.body);
        return true;
    }

    void http_response_reassembler::feed(void const* data, std::size_t length)
    {
        m_framing.feed(data, length);
    }

    bool http_response_reassembler::next(http_response& out)
    {
        http_frame frame;
        if (!m_framing.next(frame))
        {
            return false;
        }
        out = http_response{};
        parse_status_line(frame.start_line, out);
        out.headers = std::move(frame.headers);
        out.body = std::move(frame.body);
        return true;
    }
} // namespace m::mwin32_impl
