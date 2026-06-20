// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// wirecapture_harness.h — reusable in-process wire-capture harness
// (M-WIRECAP-INTEG / WC-9).
//
// Drives a tiny HTTP/1.1 server and client through one deterministic exchange
// (GET /health, POST /widgets) and returns the request/response byte streams
// teed off the connection, ready to be replayed through a `connection_capture`
// into a recording or validating sink (WC-5). The harness is transport-aware:
//
//   * ipv4 / ipv6 — server and client run on two threads in this process over a
//     genuine loopback socket; the OS assigns an ephemeral port (bind 0) which
//     the harness reads back before connecting.
//   * dns         — same, but both ends resolve "localhost" through getaddrinfo
//     so the name-resolution path is exercised; the resolved family is pinned so
//     server and client agree.
//   * synthetic   — no Winsock at all; the request and response streams are
//     built in process (the in-process synthetic edge).
//
// Across every transport the *content* of the two streams is identical, which is
// the transport-independence result the integration tests assert (WC-11).
//
// The harness only observes bytes (D6): it never mutates the wire. Fault
// injection (WC-10) makes the client send a non-conforming request body and/or
// the server return a non-conforming response body; neither breaks the
// connection — the exchange still completes, and the contract violation is
// surfaced only through the capture pipeline.
//

#ifndef M_MWIN32_TEST_WIRECAPTURE_HARNESS_H
#define M_MWIN32_TEST_WIRECAPTURE_HARNESS_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace m::mwin32_impl::wirecapture_test
{
    using namespace std::string_view_literals;

    // Transport selector for the harness. See the file header for semantics.
    enum class harness_transport
    {
        ipv4,
        ipv6,
        dns,
        synthetic
    };

    // What a single exchange teed off the connection: the client -> server
    // stream (one or more requests) and the server -> client stream (the matching
    // responses), in FIFO order so a `connection_capture` pairs them correctly.
    struct captured_streams
    {
        std::vector<std::uint8_t> requests;
        std::vector<std::uint8_t> responses;
    };

    // Knobs for one run. Fault injection is per-direction and independent.
    struct harness_options
    {
        harness_transport transport       = harness_transport::ipv4;
        bool              fault_request    = false; // client sends a bad POST body
        bool              fault_response   = false; // server returns a bad health body
    };

    namespace detail
    {
        inline constexpr std::string_view k_crlf             = "\r\n"sv;
        inline constexpr std::string_view k_header_terminator = "\r\n\r\n"sv;
        inline constexpr std::string_view k_content_length    = "content-length"sv;
        inline constexpr int              k_listen_backlog     = 1;
        inline constexpr std::size_t      k_recv_chunk         = 4096;

        inline constexpr int k_status_ok          = 200;
        inline constexpr int k_status_created      = 201;
        inline constexpr int k_status_not_found    = 404;

        inline constexpr std::string_view k_health_body_ok    = R"({"status":"ok"})"sv;
        inline constexpr std::string_view k_health_body_fault = R"({"status":0})"sv;
        inline constexpr std::string_view k_widget_body_ok    = R"({"name":"widget","size":3})"sv;
        inline constexpr std::string_view k_widget_body_fault = R"({"name":123})"sv;
        inline constexpr std::string_view k_widget_response   =
            R"({"id":1,"name":"widget","size":3})"sv;
        inline constexpr std::string_view k_not_found_body    = R"({"error":"not found"})"sv;

        inline std::string
        to_lower(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        // Parses the Content-Length value out of a header block (the text up to
        // and including the blank-line terminator). Returns 0 when absent.
        inline std::size_t
        content_length_of(std::string_view header_block)
        {
            std::string const lowered = to_lower(header_block);
            std::size_t const at = lowered.find(k_content_length);
            if (at == std::string::npos)
                return 0;

            std::size_t cursor = at + k_content_length.size();
            // Skip optional whitespace and the mandatory ':'.
            while (cursor < lowered.size() && (lowered[cursor] == ' ' || lowered[cursor] == '\t'))
                ++cursor;
            if (cursor >= lowered.size() || lowered[cursor] != ':')
                return 0;
            ++cursor;
            while (cursor < lowered.size() && (lowered[cursor] == ' ' || lowered[cursor] == '\t'))
                ++cursor;

            std::size_t value = 0;
            while (cursor < lowered.size() && lowered[cursor] >= '0' && lowered[cursor] <= '9')
            {
                value = value * 10 + static_cast<std::size_t>(lowered[cursor] - '0');
                ++cursor;
            }
            return value;
        }

        inline std::string
        make_request(std::string_view method, std::string_view target, std::string_view body)
        {
            std::string r;
            r += method;
            r += ' ';
            r += target;
            r += " HTTP/1.1";
            r += k_crlf;
            r += "Host: localhost";
            r += k_crlf;
            r += "Content-Length: ";
            r += std::to_string(body.size());
            r += k_crlf;
            r += k_crlf;
            r += body;
            return r;
        }

        inline std::string
        make_response(int status, std::string_view reason, std::string_view body)
        {
            std::string r;
            r += "HTTP/1.1 ";
            r += std::to_string(status);
            r += ' ';
            r += reason;
            r += k_crlf;
            r += "Content-Type: application/json";
            r += k_crlf;
            r += "Content-Length: ";
            r += std::to_string(body.size());
            r += k_crlf;
            r += k_crlf;
            r += body;
            return r;
        }

        // The server's routing logic, shared by every transport. `fault_response`
        // makes GET /health return a schema-violating body (a number where the
        // contract expects a string) while still answering 200 — a contract
        // violation that does not break the connection.
        inline std::string
        serve(std::string_view method, std::string_view target, bool fault_response)
        {
            if (method == "GET"sv && target == "/health"sv)
            {
                std::string_view const body =
                    fault_response ? k_health_body_fault : k_health_body_ok;
                return make_response(k_status_ok, "OK"sv, body);
            }
            if (method == "POST"sv && target == "/widgets"sv)
                return make_response(k_status_created, "Created"sv, k_widget_response);
            return make_response(k_status_not_found, "Not Found"sv, k_not_found_body);
        }

        // The scripted client requests for one exchange. The POST body is the
        // injection point for a request-direction fault (a number where the
        // contract expects a string, and a missing required field).
        inline std::vector<std::string>
        client_script(bool fault_request)
        {
            std::vector<std::string> script;
            script.push_back(make_request("GET"sv, "/health"sv, ""sv));
            script.push_back(make_request(
                "POST"sv, "/widgets"sv, fault_request ? k_widget_body_fault : k_widget_body_ok));
            return script;
        }

        inline void
        append_bytes(std::vector<std::uint8_t>& out, std::string_view s)
        {
            out.insert(out.end(), s.begin(), s.end());
        }

        // Reads exactly one Content-Length-framed HTTP message off `s`, using and
        // updating the persistent `buf` carried between messages on the same
        // connection. When `tee` is non-null, every byte received from the socket
        // is appended to it (the observational copy). Returns false if the peer
        // closed before a full message arrived.
        inline bool
        read_message(SOCKET s, std::string& buf, std::string& out, std::vector<std::uint8_t>* tee)
        {
            for (;;)
            {
                std::size_t const term = buf.find(k_header_terminator);
                if (term != std::string::npos)
                {
                    std::size_t const header_end = term + k_header_terminator.size();
                    std::size_t const body_len    = content_length_of(buf.substr(0, header_end));
                    std::size_t const total       = header_end + body_len;
                    if (buf.size() >= total)
                    {
                        out = buf.substr(0, total);
                        buf.erase(0, total);
                        return true;
                    }
                }

                char        chunk[k_recv_chunk];
                int const   got = ::recv(s, chunk, static_cast<int>(sizeof(chunk)), 0);
                if (got <= 0)
                    return false;
                if (tee != nullptr)
                    tee->insert(tee->end(), chunk, chunk + got);
                buf.append(chunk, static_cast<std::size_t>(got));
            }
        }

        inline bool
        send_all(SOCKET s, std::string_view data)
        {
            std::size_t sent = 0;
            while (sent < data.size())
            {
                int const n = ::send(
                    s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
                if (n <= 0)
                    return false;
                sent += static_cast<std::size_t>(n);
            }
            return true;
        }

        inline void
        split_request_line(std::string_view message, std::string& method, std::string& target)
        {
            std::size_t const line_end = message.find(k_crlf);
            std::string_view const line =
                message.substr(0, line_end == std::string_view::npos ? message.size() : line_end);
            std::size_t const sp1 = line.find(' ');
            if (sp1 == std::string_view::npos)
                return;
            std::size_t const sp2 = line.find(' ', sp1 + 1);
            method.assign(line.substr(0, sp1));
            target.assign(line.substr(sp1 + 1, sp2 == std::string_view::npos ? std::string_view::npos
                                                                             : sp2 - (sp1 + 1)));
        }
    } // namespace detail

    //
    // The harness. Construct with options, call run(), then read captured().
    // run() returns false (with a reason in error()) on a setup failure so that
    // the owning test can ASSERT on it.
    //
    class wire_capture_harness
    {
    public:
        explicit wire_capture_harness(harness_options opts) : m_opts(opts) {}

        bool
        run()
        {
            if (m_opts.transport == harness_transport::synthetic)
                return run_synthetic();
            return run_sockets();
        }

        captured_streams const&
        captured() const
        {
            return m_streams;
        }

        std::string const&
        error() const
        {
            return m_error;
        }

    private:
        bool
        fail(std::string reason)
        {
            m_error = std::move(reason);
            return false;
        }

        // No-socket path: build the two streams directly from the same routing
        // logic the socket path runs over the wire.
        bool
        run_synthetic()
        {
            for (auto const& request : detail::client_script(m_opts.fault_request))
            {
                detail::append_bytes(m_streams.requests, request);

                std::string method;
                std::string target;
                detail::split_request_line(request, method, target);
                detail::append_bytes(
                    m_streams.responses, detail::serve(method, target, m_opts.fault_response));
            }
            return true;
        }

        // Builds the server bind address (loopback, port 0) for the transport,
        // returning the address family chosen.
        bool
        make_bind_addr(sockaddr_storage& ss, int& len, int& family)
        {
            ss = sockaddr_storage{};
            if (m_opts.transport == harness_transport::ipv4)
            {
                auto* a            = reinterpret_cast<sockaddr_in*>(&ss);
                a->sin_family      = AF_INET;
                a->sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
                a->sin_port        = 0;
                len                = sizeof(sockaddr_in);
                family             = AF_INET;
                return true;
            }
            if (m_opts.transport == harness_transport::ipv6)
            {
                auto* a        = reinterpret_cast<sockaddr_in6*>(&ss);
                a->sin6_family = AF_INET6;
                a->sin6_addr   = in6addr_loopback;
                a->sin6_port   = 0;
                len            = sizeof(sockaddr_in6);
                family         = AF_INET6;
                return true;
            }

            // dns: resolve "localhost" with an ephemeral port and bind the first
            // result; pin its family for the matching connect.
            addrinfo hints{};
            hints.ai_family   = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            addrinfo* results = nullptr;
            if (::getaddrinfo("localhost", "0", &hints, &results) != 0 || results == nullptr)
                return false;

            std::memcpy(&ss, results->ai_addr, results->ai_addrlen);
            len    = static_cast<int>(results->ai_addrlen);
            family = results->ai_family;
            ::freeaddrinfo(results);
            return true;
        }

        // Builds the client connect address to loopback:port for the given
        // family. For dns this re-resolves "localhost" pinned to the bound family.
        bool
        make_connect_addr(int family, unsigned short port, sockaddr_storage& ss, int& len)
        {
            ss = sockaddr_storage{};
            if (m_opts.transport == harness_transport::dns)
            {
                addrinfo hints{};
                hints.ai_family   = family;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;

                addrinfo* results = nullptr;
                if (::getaddrinfo("localhost", std::to_string(port).c_str(), &hints, &results) != 0 ||
                    results == nullptr)
                    return false;

                std::memcpy(&ss, results->ai_addr, results->ai_addrlen);
                len = static_cast<int>(results->ai_addrlen);
                ::freeaddrinfo(results);
                return true;
            }

            if (family == AF_INET)
            {
                auto* a            = reinterpret_cast<sockaddr_in*>(&ss);
                a->sin_family      = AF_INET;
                a->sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
                a->sin_port        = ::htons(port);
                len                = sizeof(sockaddr_in);
                return true;
            }

            auto* a        = reinterpret_cast<sockaddr_in6*>(&ss);
            a->sin6_family = AF_INET6;
            a->sin6_addr   = in6addr_loopback;
            a->sin6_port   = ::htons(port);
            len            = sizeof(sockaddr_in6);
            return true;
        }

        bool
        run_sockets()
        {
            WSADATA wsa{};
            if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
                return fail("WSAStartup failed");

            bool const ok = run_sockets_inner();
            ::WSACleanup();
            return ok;
        }

        bool
        run_sockets_inner()
        {
            sockaddr_storage bind_ss{};
            int              bind_len    = 0;
            int              family      = 0;
            if (!make_bind_addr(bind_ss, bind_len, family))
                return fail("address resolution failed");

            SOCKET listener = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (listener == INVALID_SOCKET)
                return fail("listener socket() failed");

            if (::bind(listener, reinterpret_cast<sockaddr*>(&bind_ss), bind_len) != 0 ||
                ::listen(listener, detail::k_listen_backlog) != 0)
            {
                ::closesocket(listener);
                return fail("bind/listen failed");
            }

            sockaddr_storage bound{};
            int              bound_len = sizeof(bound);
            if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_len) != 0)
            {
                ::closesocket(listener);
                return fail("getsockname failed");
            }
            unsigned short const port =
                ::ntohs(family == AF_INET ? reinterpret_cast<sockaddr_in*>(&bound)->sin_port
                                          : reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);

            sockaddr_storage connect_ss{};
            int              connect_len = 0;
            if (!make_connect_addr(family, port, connect_ss, connect_len))
            {
                ::closesocket(listener);
                return fail("connect address resolution failed");
            }

            bool server_ok = false;
            std::thread server([&] {
                SOCKET accepted = ::accept(listener, nullptr, nullptr);
                if (accepted == INVALID_SOCKET)
                    return;

                std::string buf;
                for (;;)
                {
                    std::string message;
                    if (!detail::read_message(accepted, buf, message, nullptr))
                        break;

                    std::string method;
                    std::string target;
                    detail::split_request_line(message, method, target);
                    if (!detail::send_all(
                            accepted, detail::serve(method, target, m_opts.fault_response)))
                        break;
                }
                ::closesocket(accepted);
                server_ok = true;
            });

            bool client_ok = run_client(family, connect_ss, connect_len);

            server.join();
            ::closesocket(listener);

            if (!client_ok)
                return fail(m_error.empty() ? "client exchange failed" : m_error);
            if (!server_ok)
                return fail("server thread did not complete");
            return true;
        }

        // Connects, drives the scripted exchange, and tees the request bytes
        // (everything sent) and response bytes (everything received).
        bool
        run_client(int family, sockaddr_storage const& addr, int addr_len)
        {
            SOCKET client = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (client == INVALID_SOCKET)
                return fail("client socket() failed");

            if (::connect(client, reinterpret_cast<sockaddr const*>(&addr), addr_len) != 0)
            {
                ::closesocket(client);
                return fail("connect failed");
            }

            std::string buf;
            for (auto const& request : detail::client_script(m_opts.fault_request))
            {
                detail::append_bytes(m_streams.requests, request);
                if (!detail::send_all(client, request))
                {
                    ::closesocket(client);
                    return fail("client send failed");
                }

                std::string response;
                if (!detail::read_message(client, buf, response, &m_streams.responses))
                {
                    ::closesocket(client);
                    return fail("client did not receive a full response");
                }
            }

            ::closesocket(client);
            return true;
        }

        harness_options m_opts;
        captured_streams m_streams;
        std::string      m_error;
    };
} // namespace m::mwin32_impl::wirecapture_test

#endif // M_MWIN32_TEST_WIRECAPTURE_HARNESS_H
