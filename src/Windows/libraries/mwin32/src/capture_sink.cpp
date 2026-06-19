// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "capture_sink.h"

#include <cstdint>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace m::mwin32_impl
{
    namespace
    {
        //
        // Convert the wire-shaped headers (D21) into the PIL contract header
        // vocabulary (name/value pairs). The two surfaces deliberately share
        // the same vocabulary so no semantic translation is needed.
        //
        std::vector<m::pil::http_header>
        to_pil_headers(std::vector<http_header> const& headers)
        {
            std::vector<m::pil::http_header> out;
            out.reserve(headers.size());
            for (auto const& h: headers)
                out.emplace_back(h.name, h.value);
            return out;
        }

        std::span<std::uint8_t const>
        body_span(std::vector<std::uint8_t> const& body)
        {
            return std::span<std::uint8_t const>(body.data(), body.size());
        }
    } // namespace

    void tallying_capture_sink::on_request(http_request const& request)
    {
        ++m_request_count;
        ++m_by_method[request.method];
    }

    void tallying_capture_sink::on_response(http_response const& response)
    {
        ++m_response_count;
        ++m_by_status[response.status_code];
    }

    void tallying_capture_sink::on_crossing(http_crossing const& /*crossing*/)
    {
        ++m_crossing_count;
    }

    std::size_t tallying_capture_sink::requests_with_method(
        std::string const& method) const
    {
        auto const it = m_by_method.find(method);
        return it == m_by_method.end() ? 0 : it->second;
    }

    std::size_t tallying_capture_sink::responses_with_status(
        int status_code) const
    {
        auto const it = m_by_status.find(status_code);
        return it == m_by_status.end() ? 0 : it->second;
    }

    void recording_capture_sink::on_crossing(http_crossing const& crossing)
    {
        auto const& request  = crossing.request;
        auto const& response = crossing.response;

        auto const request_headers  = to_pil_headers(request.headers);
        auto const response_headers = to_pil_headers(response.headers);

        // The recorder strips any query string and uses the observed path as
        // the operation's path template; the wire target is passed verbatim.
        m_recorder.observe_request(
            request.method, request.target, request_headers, body_span(request.body));

        m_recorder.observe_response(request.method,
                                    request.target,
                                    static_cast<std::uint16_t>(response.status_code),
                                    response_headers,
                                    body_span(response.body));
    }

    void validating_capture_sink::on_crossing(http_crossing const& crossing)
    {
        auto const& request  = crossing.request;
        auto const& response = crossing.response;

        auto const request_headers  = to_pil_headers(request.headers);
        auto const response_headers = to_pil_headers(response.headers);

        // Request direction (client -> server).
        {
            std::error_code ec;
            auto const      d = m_document.validate_request(
                request.method, request.target, request_headers, body_span(request.body), ec);

            // An operational failure (ec) is neither a check nor a violation;
            // only a clean validation result is tallied (mirrors drive_contract).
            if (!ec)
            {
                ++m_tally.requests_checked;
                if (d)
                    ++m_tally.request_violations;
            }
        }

        // Response direction (server -> client). Keyed on the request's
        // method + path so the document can select the operation.
        {
            std::error_code ec;
            auto const      d =
                m_document.validate_response(request.method,
                                             request.target,
                                             static_cast<std::uint16_t>(response.status_code),
                                             response_headers,
                                             body_span(response.body),
                                             ec);

            if (!ec)
            {
                ++m_tally.responses_checked;
                if (d)
                    ++m_tally.response_violations;
            }
        }
    }

    void connection_capture::on_request_bytes(void const* data,
                                              std::size_t length)
    {
        m_request_stream.feed(data, length);
        drain();
    }

    void connection_capture::on_response_bytes(void const* data,
                                               std::size_t length)
    {
        m_response_stream.feed(data, length);
        drain();
    }

    void connection_capture::drain()
    {
        http_request request;
        while (m_request_stream.next(request))
        {
            m_sink.on_request(request);
            m_unpaired_requests.push_back(std::move(request));
        }

        http_response response;
        while (m_response_stream.next(response))
        {
            m_sink.on_response(response);
            m_unpaired_responses.push_back(std::move(response));
        }

        while (!m_unpaired_requests.empty() && !m_unpaired_responses.empty())
        {
            http_crossing crossing;
            crossing.request = std::move(m_unpaired_requests.front());
            crossing.response = std::move(m_unpaired_responses.front());
            m_unpaired_requests.pop_front();
            m_unpaired_responses.pop_front();
            m_sink.on_crossing(crossing);
        }
    }
} // namespace m::mwin32_impl
