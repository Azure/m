// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <cstdint>

#include <m/mwin32/mwinsock.h>

#include "session.h"

//
// Winsock interception shims (mwin32 wire capture, M-WIRECAP-SOCK / WC-1). Each
// entry point mirrors the genuine ws2_32 signature, forwards to the real ws2_32
// export, and tees the bytes actually transferred into the per-socket capture
// sink owned by the process-wide session. The tee is a pure side-channel
// (DESIGN-NOTES D6 / D20): the value and the bytes returned to the caller are
// exactly what ws2_32 produced; the connection is never altered or blocked.
//
// The shims call the genuine ws2_32 functions directly (`::socket`, `::send`,
// ...). The link-time alias redirects a *client's* Win32 calls into these
// shims, but this DLL does not link the alias object, so the unqualified calls
// here bind to ws2_32 normally — there is no recursion.
//
// v1 scope (D20): only the synchronous transfer paths are teed. `send` / `recv`
// tee exactly their reported transfer count. `WSASend` / `WSARecv` are teed only
// on synchronous completion (`lpOverlapped == NULL`, immediate success);
// overlapped completions are forwarded faithfully but not teed.
//

namespace
{
    //
    // Tee up to `transferred` bytes out of a WSABUF array, walking the buffers
    // in order until the reported transfer count is exhausted. `inbound`
    // selects the capture direction.
    //
    void
    tee_wsabuf(SOCKET      s,
               LPWSABUF    buffers,
               DWORD       buffer_count,
               std::size_t transferred,
               bool        inbound)
    {
        if (buffers == nullptr)
            return;

        std::size_t remaining = transferred;
        for (DWORD i = 0; i < buffer_count && remaining > 0; ++i)
        {
            std::size_t const available = static_cast<std::size_t>(buffers[i].len);
            std::size_t const take      = remaining < available ? remaining : available;

            if (inbound)
                m::mwin32_impl::session_socket_tee_inbound(
                    static_cast<std::uintptr_t>(s), buffers[i].buf, take);
            else
                m::mwin32_impl::session_socket_tee_outbound(
                    static_cast<std::uintptr_t>(s), buffers[i].buf, take);

            remaining -= take;
        }
    }
} // namespace

SOCKET WSAAPI
msocket(int af, int type, int protocol)
{
    return ::socket(af, type, protocol);
}

int WSAAPI
mconnect(SOCKET s, const sockaddr* name, int namelen)
{
    return ::connect(s, name, namelen);
}

SOCKET WSAAPI
maccept(SOCKET s, sockaddr* addr, int* addrlen)
{
    return ::accept(s, addr, addrlen);
}

int WSAAPI
msend(SOCKET s, const char* buf, int len, int flags)
{
    int const result = ::send(s, buf, len, flags);
    if (result > 0)
        m::mwin32_impl::session_socket_tee_outbound(
            static_cast<std::uintptr_t>(s), buf, static_cast<std::size_t>(result));
    return result;
}

int WSAAPI
mrecv(SOCKET s, char* buf, int len, int flags)
{
    int const result = ::recv(s, buf, len, flags);
    if (result > 0)
        m::mwin32_impl::session_socket_tee_inbound(
            static_cast<std::uintptr_t>(s), buf, static_cast<std::size_t>(result));
    return result;
}

int WSAAPI
mclosesocket(SOCKET s)
{
    int const result = ::closesocket(s);
    m::mwin32_impl::session_socket_closed(static_cast<std::uintptr_t>(s));
    return result;
}

int WSAAPI
mWSASend(SOCKET s,
         LPWSABUF lpBuffers,
         DWORD dwBufferCount,
         LPDWORD lpNumberOfBytesSent,
         DWORD dwFlags,
         LPWSAOVERLAPPED lpOverlapped,
         LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    int const result = ::WSASend(
        s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);

    // Only the synchronous-completion path carries a reliable transfer count
    // here; an overlapped call reports its count through the OVERLAPPED later
    // (not teed in v1).
    if (result == 0 && lpOverlapped == nullptr && lpNumberOfBytesSent != nullptr)
        tee_wsabuf(s, lpBuffers, dwBufferCount,
                   static_cast<std::size_t>(*lpNumberOfBytesSent), /*inbound=*/false);

    return result;
}

int WSAAPI
mWSARecv(SOCKET s,
         LPWSABUF lpBuffers,
         DWORD dwBufferCount,
         LPDWORD lpNumberOfBytesRecvd,
         LPDWORD lpFlags,
         LPWSAOVERLAPPED lpOverlapped,
         LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    int const result = ::WSARecv(
        s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);

    if (result == 0 && lpOverlapped == nullptr && lpNumberOfBytesRecvd != nullptr)
        tee_wsabuf(s, lpBuffers, dwBufferCount,
                   static_cast<std::size_t>(*lpNumberOfBytesRecvd), /*inbound=*/true);

    return result;
}
