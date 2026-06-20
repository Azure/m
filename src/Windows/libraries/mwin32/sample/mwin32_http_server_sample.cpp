// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Sample HTTP/1.1 server for the mwin32 wire-capture demo (WC-6).
//
// This is an ORDINARY raw-Winsock server: it includes only the Winsock headers
// and calls the genuine entry points (socket / bind / listen / accept / recv /
// send / closesocket). It has no knowledge of mwin32 and includes none of its
// headers. The only thing that makes its traffic observable is that its CMake
// target links the `mwin32_alias` object, whose __imp_ slots retarget the
// data-transfer calls (socket / accept / send / recv / closesocket) into the
// mwin32 shim, which tees the bytes to the capture seam (WC-1 .. WC-5). Whether
// capture is active — and in which mode — is decided entirely outside this
// program by the `<executable>.pilcfg` sidecar; with no sidecar the shim is a
// transparent passthrough and this server behaves like any other.
//
// It serves two REST endpoints plus a shutdown control:
//   * GET  /health   -> 200 {"status":"ok"}
//   * POST /widgets  -> 201 {"id":1,"name":"widget","size":3}
//   * GET  /shutdown -> 200 {"bye":true}, then the server exits.
// Unknown routes answer 404.
//
// Topology is a runtime choice (D19, family-blind capture): `--family
// ipv4|ipv6|dual` selects the bind family, and `--port N` selects the listen
// port (0 = ephemeral; the chosen port is echoed as `port=<N>` on stdout so a
// harness can read it back). A fault switch (`--fault` or env
// `MWIN32_SAMPLE_FAULT=1`) makes GET /health emit a deliberately
// non-conforming body (`{"status":0}` — status typed as a number) WITHOUT
// breaking the connection, so the capture's validate mode can detect a
// server->client contract violation while traffic still completes.
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
    // The bind family selected by `--family`. `dual` binds an AF_INET6 socket
    // with IPV6_V6ONLY cleared so a single listener accepts both IPv4-mapped
    // and IPv6 peers.
    //
    enum class bind_family
    {
        ipv4,
        ipv6,
        dual,
    };

    //
    // The whole server configuration, parsed from argv / environment.
    //
    struct server_options
    {
        bind_family    family = bind_family::ipv4;
        unsigned short port   = 0; // 0 == ephemeral
        bool           fault  = false;
    };

    // Named constants for the small protocol surface this sample uses; no
    // magic numbers in the logic below.
    constexpr int           k_listen_backlog   = 4;
    constexpr int           k_recv_chunk        = 4096;
    constexpr std::uint16_t k_status_ok         = 200;
    constexpr std::uint16_t k_status_created     = 201;
    constexpr std::uint16_t k_status_not_found   = 404;
    constexpr std::string_view k_crlf            = "\r\n";
    constexpr std::string_view k_header_sep       = "\r\n\r\n";
    constexpr std::string_view k_content_length   = "content-length";

    //
    // Parse the command line. Unknown options are ignored (tolerant). The fault
    // switch also honors the MWIN32_SAMPLE_FAULT environment variable so a
    // harness can inject the fault without altering argv.
    //
    server_options
    parse_options(int argc, wchar_t** argv)
    {
        server_options opts;

        for (int i = 1; i < argc; ++i)
        {
            std::wstring_view const arg = argv[i];

            if (arg == L"--family" && i + 1 < argc)
            {
                std::wstring_view const value = argv[++i];
                if (value == L"ipv6")
                    opts.family = bind_family::ipv6;
                else if (value == L"dual")
                    opts.family = bind_family::dual;
                else
                    opts.family = bind_family::ipv4;
            }
            else if (arg == L"--port" && i + 1 < argc)
            {
                opts.port = static_cast<unsigned short>(::_wtoi(argv[++i]));
            }
            else if (arg == L"--fault")
            {
                opts.fault = true;
            }
        }

        wchar_t  env_buf[8] = {};
        DWORD const env_len = ::GetEnvironmentVariableW(
            L"MWIN32_SAMPLE_FAULT", env_buf, static_cast<DWORD>(std::size(env_buf)));
        if (env_len > 0 && env_buf[0] == L'1')
            opts.fault = true;

        return opts;
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

    //
    // A fully received HTTP/1.1 request: just the pieces this sample routes on.
    //
    struct request
    {
        std::string method;
        std::string target;
        std::string body;
    };

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
    // Pull one complete request out of the connection's running buffer. Returns
    // true and fills `out` (consuming the request's bytes from `buffer`) once a
    // full header block plus its Content-Length body are present; returns false
    // when more bytes are needed.
    //
    bool
    try_take_request(std::string& buffer, request& out)
    {
        std::size_t const header_end = buffer.find(k_header_sep);
        if (header_end == std::string::npos)
            return false;

        std::string_view const headers(buffer.data(), header_end);
        std::size_t const      body_len   = content_length_of(headers);
        std::size_t const      body_start = header_end + k_header_sep.size();

        if (buffer.size() < body_start + body_len)
            return false; // body not fully arrived yet

        // Parse the start line: METHOD SP request-target SP HTTP/x.y
        std::string_view const start_line =
            headers.substr(0, headers.find(k_crlf));
        std::size_t const sp1 = start_line.find(' ');
        std::size_t const sp2 = (sp1 == std::string_view::npos)
                                    ? std::string_view::npos
                                    : start_line.find(' ', sp1 + 1);

        out.method.clear();
        out.target.clear();
        out.body.clear();
        if (sp1 != std::string_view::npos && sp2 != std::string_view::npos)
        {
            out.method = std::string(start_line.substr(0, sp1));
            out.target = std::string(start_line.substr(sp1 + 1, sp2 - sp1 - 1));
        }
        out.body = buffer.substr(body_start, body_len);

        buffer.erase(0, body_start + body_len);
        return true;
    }

    //
    // Build an HTTP/1.1 response with a JSON body and an explicit
    // Content-Length so framing is unambiguous (the v1 reassembler scope, D21).
    // The connection is kept alive (no Connection: close) so the demo exercises
    // keep-alive request/response pairing.
    //
    std::string
    make_response(std::uint16_t status, std::string_view reason, std::string_view json_body)
    {
        std::string response;
        response.reserve(json_body.size() + 128);
        response += "HTTP/1.1 ";
        response += std::to_string(status);
        response += ' ';
        response += reason;
        response += k_crlf;
        response += "Content-Type: application/json";
        response += k_crlf;
        response += "Content-Length: ";
        response += std::to_string(json_body.size());
        response += k_crlf;
        response += k_crlf;
        response += json_body;
        return response;
    }

    //
    // Route a request to its response. Sets `*shutdown` when the control
    // endpoint asks the server to exit. The fault switch makes GET /health emit
    // a non-conforming body without otherwise disturbing the exchange.
    //
    std::string
    route(request const& req, bool fault, bool* shutdown)
    {
        if (req.method == "GET" && req.target == "/health")
        {
            std::string_view const body =
                fault ? std::string_view(R"({"status":0})")
                      : std::string_view(R"({"status":"ok"})");
            return make_response(k_status_ok, "OK", body);
        }

        if (req.method == "POST" && req.target == "/widgets")
        {
            return make_response(
                k_status_created, "Created", R"({"id":1,"name":"widget","size":3})");
        }

        if (req.method == "GET" && req.target == "/shutdown")
        {
            *shutdown = true;
            return make_response(k_status_ok, "OK", R"({"bye":true})");
        }

        return make_response(k_status_not_found, "Not Found", R"({"error":"not found"})");
    }

    //
    // Serve one accepted connection: read requests, answer each, keep the
    // connection alive until the peer closes or a shutdown is requested.
    //
    void
    serve_connection(SOCKET conn, bool fault, bool* shutdown)
    {
        std::string buffer;
        std::vector<char> chunk(static_cast<std::size_t>(k_recv_chunk));

        for (;;)
        {
            request req;
            while (try_take_request(buffer, req))
            {
                std::string const response = route(req, fault, shutdown);
                if (!send_all(conn, response.data(), static_cast<int>(response.size())))
                    return;
                if (*shutdown)
                    return;
            }

            int const n = ::recv(conn, chunk.data(), k_recv_chunk, 0);
            if (n <= 0)
                return; // peer closed or error
            buffer.append(chunk.data(), static_cast<std::size_t>(n));
        }
    }

    //
    // Create, bind, and listen a TCP socket for the selected family. On success
    // returns the listening socket and reports the actually-bound port through
    // `bound_port`; on failure returns INVALID_SOCKET.
    //
    SOCKET
    make_listener(server_options const& opts, reporter const& report, unsigned short* bound_port)
    {
        int const af = (opts.family == bind_family::ipv4) ? AF_INET : AF_INET6;

        SOCKET const listener = ::socket(af, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            report.kv("error", "socket");
            return INVALID_SOCKET;
        }

        if (opts.family == bind_family::dual)
        {
            DWORD v6only = 0;
            ::setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY,
                         reinterpret_cast<char const*>(&v6only), sizeof(v6only));
        }

        int bind_rc = SOCKET_ERROR;
        if (af == AF_INET)
        {
            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
            addr.sin_port        = ::htons(opts.port);
            bind_rc = ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        }
        else
        {
            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_addr   = (opts.family == bind_family::dual) ? in6addr_any
                                                                  : in6addr_loopback;
            addr.sin6_port   = ::htons(opts.port);
            bind_rc = ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        }

        if (bind_rc == SOCKET_ERROR)
        {
            report.kv("error", "bind");
            ::closesocket(listener);
            return INVALID_SOCKET;
        }

        if (::listen(listener, k_listen_backlog) == SOCKET_ERROR)
        {
            report.kv("error", "listen");
            ::closesocket(listener);
            return INVALID_SOCKET;
        }

        // Read back the actually-bound port (matters when --port 0 asked for an
        // ephemeral port so the harness can connect to it).
        sockaddr_storage bound{};
        int              bound_len = sizeof(bound);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0)
        {
            if (bound.ss_family == AF_INET)
                *bound_port = ::ntohs(reinterpret_cast<sockaddr_in const*>(&bound)->sin_port);
            else
                *bound_port = ::ntohs(reinterpret_cast<sockaddr_in6 const*>(&bound)->sin6_port);
        }

        return listener;
    }

    char const*
    family_name(bind_family f)
    {
        switch (f)
        {
        case bind_family::ipv6:
            return "ipv6";
        case bind_family::dual:
            return "dual";
        case bind_family::ipv4:
        default:
            return "ipv4";
        }
    }
} // namespace

int
wmain(int argc, wchar_t** argv)
{
    reporter const       report(stdout);
    server_options const opts = parse_options(argc, argv);

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        report.kv("error", "wsastartup");
        return 1;
    }

    unsigned short bound_port = 0;
    SOCKET const   listener   = make_listener(opts, report, &bound_port);
    if (listener == INVALID_SOCKET)
    {
        ::WSACleanup();
        return 1;
    }

    report.kv("family", family_name(opts.family));
    report.kv("port", static_cast<unsigned long>(bound_port));
    report.kv("fault", opts.fault ? "1" : "0");
    report.kv("ready", 1ul);

    bool shutdown = false;
    while (!shutdown)
    {
        SOCKET const conn = ::accept(listener, nullptr, nullptr);
        if (conn == INVALID_SOCKET)
            break;

        serve_connection(conn, opts.fault, &shutdown);
        ::closesocket(conn);
    }

    ::closesocket(listener);
    ::WSACleanup();
    report.kv("stopped", 1ul);
    return 0;
}
