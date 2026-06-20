// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Sample HTTP/1.1 client for the mwin32 wire-capture demo (WC-7).
//
// This is an ORDINARY raw-Winsock client: it includes only the Winsock headers
// and calls the genuine entry points (getaddrinfo / socket / connect / send /
// recv / closesocket). It has no knowledge of mwin32 and includes none of its
// headers. The only thing that makes its traffic observable is that its CMake
// target links the `mwin32_alias` object, whose __imp_ slots retarget the
// data-transfer calls (socket / connect / send / recv / closesocket) into the
// mwin32 shim, which tees the bytes to the capture seam (WC-1 .. WC-5). Whether
// capture is active — and in which mode — is decided entirely outside this
// program by the `<executable>.pilcfg` sidecar; with no sidecar the shim is a
// transparent passthrough and this client behaves like any other.
//
// It drives the server sample's endpoints:
//   * GET  /health   (expects 200 {"status":"ok"})
//   * POST /widgets  (sends {"name":"widget","size":3}; expects 201)
//   * GET  /shutdown (only with --shutdown; asks the server to exit)
//
// Topology is a runtime choice (D19, family-blind capture): `--target` selects
// how the peer is reached:
//   * dns:<host>:<port>  -> getaddrinfo(<host>) (the DNS-resolved path)
//   * ipv4:<port>        -> literal 127.0.0.1
//   * ipv6:<port>        -> literal ::1
// A fault switch (`--fault` or env `MWIN32_SAMPLE_FAULT=1`) makes POST /widgets
// send a deliberately non-conforming request body (`{"name":123}` — name typed
// as a number, size omitted) WITHOUT breaking the connection, so the capture's
// validate mode can detect a client->server contract violation while the
// exchange still completes.
//

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    //
    // The single output site for the whole program. Per the repository's
    // architectural pre-step rule, all reporting is routed through one sink so
    // the destination concern is separable from the call sites. Status lines
    // are emitted as `tag=value` so a harness can parse them without ambiguity.
    //
    class reporter
    {
    public:
        explicit reporter(std::FILE* out) noexcept : m_out(out) {}

        void
        kv(std::string_view tag, std::string_view value) const
        {
            std::fprintf(m_out, "%.*s=%.*s\n",
                         static_cast<int>(tag.size()), tag.data(),
                         static_cast<int>(value.size()), value.data());
            std::fflush(m_out);
        }

        void
        kv(std::string_view tag, unsigned long value) const
        {
            std::fprintf(m_out, "%.*s=%lu\n",
                         static_cast<int>(tag.size()), tag.data(), value);
            std::fflush(m_out);
        }

    private:
        std::FILE* m_out;
    };

    //
    // How the peer address is obtained. `dns` runs the name through
    // `getaddrinfo`; `ipv4`/`ipv6` use the literal loopback address for the
    // family (no name resolution), mirroring the server's bind families.
    //
    enum class target_kind
    {
        dns,
        ipv4,
        ipv6,
    };

    //
    // The whole client configuration, parsed from argv / environment.
    //
    struct client_options
    {
        target_kind    kind     = target_kind::ipv4;
        std::string    host     = "127.0.0.1"; // used for dns; also the Host header value
        unsigned short port     = 0;
        bool           fault    = false;
        bool           shutdown = false;
    };

    // Named constants for the small protocol surface this sample uses; no
    // magic numbers in the logic below.
    constexpr int              k_recv_chunk    = 4096;
    constexpr std::string_view k_crlf          = "\r\n";
    constexpr std::string_view k_header_sep    = "\r\n\r\n";
    constexpr std::string_view k_content_length = "content-length";

    // The conforming and faulted request bodies for POST /widgets. The faulted
    // body fails the derived request schema (name typed as a number, size
    // omitted) while remaining a well-framed HTTP request.
    constexpr std::string_view k_widget_body_ok    = R"({"name":"widget","size":3})";
    constexpr std::string_view k_widget_body_fault = R"({"name":123})";

    //
    // Parse `--target <selector>` into kind/host/port. Selectors:
    //   dns:<host>:<port>   ipv4:<port>   ipv6:<port>
    // Returns false on a malformed selector.
    //
    bool
    parse_target(std::string_view selector, client_options& opts)
    {
        constexpr std::string_view k_dns  = "dns:";
        constexpr std::string_view k_ipv4 = "ipv4:";
        constexpr std::string_view k_ipv6 = "ipv6:";

        if (selector.starts_with(k_dns))
        {
            std::string_view const rest = selector.substr(k_dns.size());
            std::size_t const      colon = rest.rfind(':');
            if (colon == std::string_view::npos || colon == 0)
                return false;
            opts.kind = target_kind::dns;
            opts.host = std::string(rest.substr(0, colon));
            opts.port = static_cast<unsigned short>(
                ::strtoul(std::string(rest.substr(colon + 1)).c_str(), nullptr, 10));
            return opts.port != 0;
        }

        if (selector.starts_with(k_ipv4))
        {
            opts.kind = target_kind::ipv4;
            opts.host = "127.0.0.1";
            opts.port = static_cast<unsigned short>(
                ::strtoul(std::string(selector.substr(k_ipv4.size())).c_str(), nullptr, 10));
            return opts.port != 0;
        }

        if (selector.starts_with(k_ipv6))
        {
            opts.kind = target_kind::ipv6;
            opts.host = "::1";
            opts.port = static_cast<unsigned short>(
                ::strtoul(std::string(selector.substr(k_ipv6.size())).c_str(), nullptr, 10));
            return opts.port != 0;
        }

        return false;
    }

    //
    // Parse the command line. Unknown options are ignored (tolerant). The fault
    // switch also honors the MWIN32_SAMPLE_FAULT environment variable so a
    // harness can inject the fault without altering argv. Returns false if the
    // required --target is missing or malformed.
    //
    bool
    parse_options(int argc, wchar_t** argv, client_options& opts)
    {
        bool have_target = false;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring_view const arg = argv[i];

            if (arg == L"--target" && i + 1 < argc)
            {
                // The selector is plain ASCII; narrow it without code-page
                // surprises (host names here are loopback literals or simple
                // DNS labels).
                std::wstring_view const wsel = argv[++i];
                std::string             selector;
                selector.reserve(wsel.size());
                for (wchar_t const c : wsel)
                    selector.push_back(static_cast<char>(c & 0x7f));
                have_target = parse_target(selector, opts);
            }
            else if (arg == L"--fault")
            {
                opts.fault = true;
            }
            else if (arg == L"--shutdown")
            {
                opts.shutdown = true;
            }
        }

        wchar_t  env_buf[8] = {};
        DWORD const env_len = ::GetEnvironmentVariableW(
            L"MWIN32_SAMPLE_FAULT", env_buf, static_cast<DWORD>(std::size(env_buf)));
        if (env_len > 0 && env_buf[0] == L'1')
            opts.fault = true;

        return have_target;
    }

    //
    // Send the whole buffer, looping over partial sends. Returns false if the
    // peer went away mid-write.
    //
    bool
    send_all(SOCKET s, char const* data, int length)
    {
        int sent = 0;
        while (sent < length)
        {
            int const n = ::send(s, data + sent, length - sent, 0);
            if (n == SOCKET_ERROR || n == 0)
                return false;
            sent += n;
        }
        return true;
    }

    // Case-insensitive ASCII compare for header-name matching (RFC 9110).
    bool
    iequals(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            char const ca = static_cast<char>(::tolower(static_cast<unsigned char>(a[i])));
            char const cb = static_cast<char>(::tolower(static_cast<unsigned char>(b[i])));
            if (ca != cb)
                return false;
        }
        return true;
    }

    //
    // Extract the declared Content-Length from a header block, or 0 if absent.
    // The header block excludes the terminating blank line.
    //
    std::size_t
    content_length_of(std::string_view headers)
    {
        std::size_t pos = headers.find(k_crlf);
        while (pos != std::string_view::npos)
        {
            std::size_t const line_start = pos + k_crlf.size();
            std::size_t const next       = headers.find(k_crlf, line_start);
            std::string_view const line  =
                headers.substr(line_start, next == std::string_view::npos
                                               ? std::string_view::npos
                                               : next - line_start);

            std::size_t const colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view const name = line.substr(0, colon);
                if (iequals(name, k_content_length))
                {
                    std::string_view value = line.substr(colon + 1);
                    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                        value.remove_prefix(1);
                    return static_cast<std::size_t>(::strtoul(
                        std::string(value).c_str(), nullptr, 10));
                }
            }

            pos = next;
        }
        return 0;
    }

    //
    // Build an HTTP/1.1 request with an explicit Content-Length so framing is
    // unambiguous (the v1 reassembler scope, D21). The connection is kept alive
    // (no Connection: close) so the demo exercises keep-alive pairing.
    //
    std::string
    make_request(std::string_view method, std::string_view target, std::string_view host,
                 std::string_view json_body)
    {
        std::string request;
        request.reserve(json_body.size() + 128);
        request += method;
        request += ' ';
        request += target;
        request += " HTTP/1.1";
        request += k_crlf;
        request += "Host: ";
        request += host;
        request += k_crlf;
        if (!json_body.empty())
        {
            request += "Content-Type: application/json";
            request += k_crlf;
            request += "Content-Length: ";
            request += std::to_string(json_body.size());
            request += k_crlf;
        }
        else
        {
            request += "Content-Length: 0";
            request += k_crlf;
        }
        request += k_crlf;
        request += json_body;
        return request;
    }

    //
    // Read one complete HTTP/1.1 response off the connection: header block plus
    // its Content-Length body. Returns the parsed status code (0 on failure)
    // and the response body through `body`.
    //
    unsigned long
    recv_response(SOCKET conn, std::string& body)
    {
        std::string       buffer;
        std::vector<char> chunk(static_cast<std::size_t>(k_recv_chunk));

        // Pull bytes until the full header block has arrived.
        std::size_t header_end = std::string::npos;
        while ((header_end = buffer.find(k_header_sep)) == std::string::npos)
        {
            int const n = ::recv(conn, chunk.data(), k_recv_chunk, 0);
            if (n <= 0)
                return 0;
            buffer.append(chunk.data(), static_cast<std::size_t>(n));
        }

        std::string_view const headers(buffer.data(), header_end);
        std::size_t const      body_len   = content_length_of(headers);
        std::size_t const      body_start = header_end + k_header_sep.size();

        // Pull the rest of the body if it has not all arrived yet.
        while (buffer.size() < body_start + body_len)
        {
            int const n = ::recv(conn, chunk.data(), k_recv_chunk, 0);
            if (n <= 0)
                return 0;
            buffer.append(chunk.data(), static_cast<std::size_t>(n));
        }

        // Parse the status code from the start line: HTTP/x.y SP status SP reason
        std::string_view const start_line = headers.substr(0, headers.find(k_crlf));
        std::size_t const      sp1        = start_line.find(' ');
        unsigned long          status     = 0;
        if (sp1 != std::string_view::npos)
        {
            std::string_view const after = start_line.substr(sp1 + 1);
            status = ::strtoul(std::string(after).c_str(), nullptr, 10);
        }

        body.assign(buffer, body_start, body_len);
        return status;
    }

    //
    // Resolve the configured target to a connected socket. For the dns target
    // the host name is run through getaddrinfo; for the ipv4/ipv6 targets the
    // literal loopback address is used directly. Returns INVALID_SOCKET on
    // failure.
    //
    SOCKET
    connect_target(client_options const& opts, reporter const& report)
    {
        std::string const port_text = std::to_string(opts.port);

        if (opts.kind == target_kind::dns)
        {
            addrinfo hints{};
            hints.ai_family   = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            addrinfo* results = nullptr;
            if (::getaddrinfo(opts.host.c_str(), port_text.c_str(), &hints, &results) != 0
                || results == nullptr)
            {
                report.kv("error", "getaddrinfo");
                return INVALID_SOCKET;
            }

            SOCKET conn = INVALID_SOCKET;
            for (addrinfo* it = results; it != nullptr; it = it->ai_next)
            {
                conn = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
                if (conn == INVALID_SOCKET)
                    continue;
                if (::connect(conn, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0)
                    break;
                ::closesocket(conn);
                conn = INVALID_SOCKET;
            }

            ::freeaddrinfo(results);
            if (conn == INVALID_SOCKET)
                report.kv("error", "connect");
            return conn;
        }

        int const af = (opts.kind == target_kind::ipv4) ? AF_INET : AF_INET6;

        SOCKET const conn = ::socket(af, SOCK_STREAM, IPPROTO_TCP);
        if (conn == INVALID_SOCKET)
        {
            report.kv("error", "socket");
            return INVALID_SOCKET;
        }

        int connect_rc = SOCKET_ERROR;
        if (af == AF_INET)
        {
            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
            addr.sin_port        = ::htons(opts.port);
            connect_rc = ::connect(conn, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        }
        else
        {
            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_addr   = in6addr_loopback;
            addr.sin6_port   = ::htons(opts.port);
            connect_rc = ::connect(conn, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        }

        if (connect_rc == SOCKET_ERROR)
        {
            report.kv("error", "connect");
            ::closesocket(conn);
            return INVALID_SOCKET;
        }

        return conn;
    }

    //
    // Send one request and report the response status. Returns false if the
    // exchange could not complete (connection broken).
    //
    bool
    exchange(SOCKET conn, reporter const& report, std::string_view tag,
             std::string const& request)
    {
        if (!send_all(conn, request.data(), static_cast<int>(request.size())))
        {
            report.kv("error", "send");
            return false;
        }

        std::string   body;
        unsigned long const status = recv_response(conn, body);
        if (status == 0)
        {
            report.kv("error", "recv");
            return false;
        }

        report.kv(tag, status);
        return true;
    }

    char const*
    target_name(target_kind k)
    {
        switch (k)
        {
        case target_kind::dns:
            return "dns";
        case target_kind::ipv6:
            return "ipv6";
        case target_kind::ipv4:
        default:
            return "ipv4";
        }
    }
} // namespace

int
wmain(int argc, wchar_t** argv)
{
    reporter const report(stdout);

    client_options opts;
    if (!parse_options(argc, argv, opts))
    {
        report.kv("error", "target");
        return 1;
    }

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        report.kv("error", "wsastartup");
        return 1;
    }

    report.kv("target", target_name(opts.kind));
    report.kv("port", static_cast<unsigned long>(opts.port));
    report.kv("fault", opts.fault ? "1" : "0");

    SOCKET const conn = connect_target(opts, report);
    if (conn == INVALID_SOCKET)
    {
        ::WSACleanup();
        return 1;
    }

    report.kv("connected", 1ul);

    int rc = 0;

    // 1) GET /health
    if (!exchange(conn, report, "health",
                  make_request("GET", "/health", opts.host, "")))
    {
        rc = 1;
    }

    // 2) POST /widgets — conforming body, or the faulted body that violates the
    //    derived request schema (client->server contract violation).
    if (rc == 0)
    {
        std::string_view const widget_body =
            opts.fault ? k_widget_body_fault : k_widget_body_ok;
        if (!exchange(conn, report, "widgets",
                      make_request("POST", "/widgets", opts.host, widget_body)))
        {
            rc = 1;
        }
    }

    // 3) GET /shutdown — only when asked, so the harness controls server exit.
    if (rc == 0 && opts.shutdown)
    {
        if (!exchange(conn, report, "shutdown",
                      make_request("GET", "/shutdown", opts.host, "")))
        {
            rc = 1;
        }
    }

    ::closesocket(conn);
    ::WSACleanup();
    report.kv("done", 1ul);
    return rc;
}
