// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// test_mwinsock.cpp — unit tests for the Winsock wire-capture shims
// (M-WIRECAP-SOCK / WC-1).
//
// Two groups, deliberately separated because the shims execute inside
// m_mwin32.dll (which owns one process-wide session singleton) while this test
// links m_mwin32_internal directly (its own copy of that singleton). The two
// singletons are distinct, so a capture written by a DLL shim is not readable
// through the in-test session accessors and vice-versa.
//
//   * MWinSockTee — exercises the per-socket capture mechanism directly through
//     the in-test session, verifying accumulation, direction separation, and
//     release-on-close.
//   * MWinSockPassthrough — drives the real DLL shims over a genuine IPv4
//     loopback connection and asserts the bytes the caller receives are
//     byte-identical to those sent (the tee is a pure side-channel that never
//     mutates the stream).
//

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <m/mwin32/mwinsock.h>

#include "session.h"

#include <gtest/gtest.h>

namespace
{
    std::vector<std::uint8_t>
    to_bytes(std::string_view s)
    {
        return std::vector<std::uint8_t>(s.begin(), s.end());
    }
} // namespace

//
// ---- MWinSockTee: per-socket capture mechanism (in-test session) -----------
//

TEST(MWinSockTee, OutboundAndInboundAccumulateSeparately)
{
    // A socket value that no real socket in this process will mint.
    constexpr std::uintptr_t s = 700001;

    m::mwin32_impl::session_socket_closed(s); // clean slate

    m::mwin32_impl::session_socket_tee_outbound(s, "GET ", 4);
    m::mwin32_impl::session_socket_tee_outbound(s, "/pets", 5);
    m::mwin32_impl::session_socket_tee_inbound(s, "200 OK", 6);

    EXPECT_EQ(m::mwin32_impl::session_socket_captured_outbound(s), to_bytes("GET /pets"));
    EXPECT_EQ(m::mwin32_impl::session_socket_captured_inbound(s), to_bytes("200 OK"));

    m::mwin32_impl::session_socket_closed(s);
}

TEST(MWinSockTee, ZeroLengthAndNullAreIgnored)
{
    constexpr std::uintptr_t s = 700002;
    m::mwin32_impl::session_socket_closed(s);

    m::mwin32_impl::session_socket_tee_outbound(s, "abc", 0);
    m::mwin32_impl::session_socket_tee_outbound(s, nullptr, 5);
    m::mwin32_impl::session_socket_tee_inbound(s, nullptr, 0);

    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_outbound(s).empty());
    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_inbound(s).empty());

    m::mwin32_impl::session_socket_closed(s);
}

TEST(MWinSockTee, CloseReleasesCapture)
{
    constexpr std::uintptr_t s = 700003;
    m::mwin32_impl::session_socket_closed(s);

    m::mwin32_impl::session_socket_tee_outbound(s, "payload", 7);
    EXPECT_EQ(m::mwin32_impl::session_socket_captured_outbound(s), to_bytes("payload"));

    m::mwin32_impl::session_socket_closed(s);

    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_outbound(s).empty());
    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_inbound(s).empty());
}

TEST(MWinSockTee, UnknownSocketHasEmptyCapture)
{
    constexpr std::uintptr_t s = 700004;
    m::mwin32_impl::session_socket_closed(s);

    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_outbound(s).empty());
    EXPECT_TRUE(m::mwin32_impl::session_socket_captured_inbound(s).empty());
}

//
// ---- MWinSockPassthrough: real loopback through the DLL shims --------------
//

namespace
{
    class LoopbackFixture: public ::testing::Test
    {
    protected:
        void
        SetUp() override
        {
            WSADATA wsa{};
            ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa), 0);

            // Listener: created through the shim, bound/listened with genuine
            // ws2_32 (bind/listen are not part of the wire-capture shim set).
            m_listener = msocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            ASSERT_NE(m_listener, INVALID_SOCKET);

            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port        = 0; // ephemeral
            ASSERT_EQ(::bind(m_listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
            ASSERT_EQ(::listen(m_listener, 1), 0);

            // Read back the ephemeral port the OS assigned.
            sockaddr_in bound{};
            int         bound_len = sizeof(bound);
            ASSERT_EQ(::getsockname(m_listener, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);

            // Client connects through the shim; on loopback the handshake
            // completes against the listen backlog without an explicit accept,
            // so a single-threaded connect-then-accept is deterministic.
            m_client = msocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            ASSERT_NE(m_client, INVALID_SOCKET);
            ASSERT_EQ(mconnect(m_client, reinterpret_cast<sockaddr*>(&bound), sizeof(bound)), 0);

            m_accepted = maccept(m_listener, nullptr, nullptr);
            ASSERT_NE(m_accepted, INVALID_SOCKET);
        }

        void
        TearDown() override
        {
            if (m_client != INVALID_SOCKET)
                mclosesocket(m_client);
            if (m_accepted != INVALID_SOCKET)
                mclosesocket(m_accepted);
            if (m_listener != INVALID_SOCKET)
                mclosesocket(m_listener);
            WSACleanup();
        }

        static bool
        send_all(SOCKET s, std::string_view data)
        {
            std::size_t total = 0;
            while (total < data.size())
            {
                int const n = msend(
                    s, data.data() + total, static_cast<int>(data.size() - total), 0);
                if (n <= 0)
                    return false;
                total += static_cast<std::size_t>(n);
            }
            return true;
        }

        static std::string
        recv_exact(SOCKET s, std::size_t count)
        {
            std::string out;
            out.reserve(count);
            char buf[1024];
            while (out.size() < count)
            {
                int const want = static_cast<int>(std::min(sizeof(buf), count - out.size()));
                int const got  = mrecv(s, buf, want, 0);
                if (got <= 0)
                    break;
                out.append(buf, static_cast<std::size_t>(got));
            }
            return out;
        }

        SOCKET m_listener = INVALID_SOCKET;
        SOCKET m_client   = INVALID_SOCKET;
        SOCKET m_accepted = INVALID_SOCKET;
    };
} // namespace

TEST_F(LoopbackFixture, SendRecvIsByteIdentical)
{
    std::string const request  = "GET /pets HTTP/1.1\r\nHost: localhost\r\n\r\n";
    std::string const response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]";

    // client -> server
    ASSERT_TRUE(send_all(m_client, request));
    EXPECT_EQ(recv_exact(m_accepted, request.size()), request);

    // server -> client
    ASSERT_TRUE(send_all(m_accepted, response));
    EXPECT_EQ(recv_exact(m_client, response.size()), response);
}

TEST_F(LoopbackFixture, WsaSendRecvIsByteIdentical)
{
    std::string const request = "POST /pets HTTP/1.1\r\nContent-Length: 4\r\n\r\nfido";

    WSABUF sbuf{};
    sbuf.len = static_cast<ULONG>(request.size());
    sbuf.buf = const_cast<CHAR*>(request.data());

    DWORD     sent = 0;
    int const rc   = mWSASend(m_client, &sbuf, 1, &sent, 0, nullptr, nullptr);
    ASSERT_EQ(rc, 0);
    EXPECT_EQ(sent, request.size());

    // A small message on loopback arrives in a single segment.
    std::string const received = recv_exact(m_accepted, request.size());
    EXPECT_EQ(received, request);
}
