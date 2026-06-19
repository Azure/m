// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#undef NOMINMAX
#define NOMINMAX

//
// winsock2.h must be included before windows.h so the modern Winsock 2 surface
// (and not the legacy winsock.h v1 declarations windows.h would otherwise pull)
// is the one in scope. Including it here keeps that ordering correct for any
// consumer of the wire-capture shims.
//
#include <winsock2.h>
#include <ws2tcpip.h>

//
// Winsock interception shims (mwin32 wire capture, M-WIRECAP-SOCK). These entry
// points mirror the genuine ws2_32 exports (`socket`, `connect`, `accept`,
// `send`, `recv`, `closesocket`, `WSASend`, `WSARecv`) so an unmodified client
// redirects through the generated mwin32_alias object with no source change.
//
// Each shim forwards to the real ws2_32 function and *tees* the bytes actually
// transferred into a per-socket capture sink. The tee is a pure side-channel
// (DESIGN-NOTES D6 / D20): the bytes returned to the caller are exactly the
// bytes ws2_32 produced and the connection is never altered or blocked. The
// reassembly of the teed byte stream into HTTP messages and the record/validate
// sink are layered on top (later wire-capture milestones).
//
// Scope (DESIGN-NOTES D20): only the synchronous transfer paths are teed —
// `send`/`recv`, and the synchronous-completion path of `WSASend`/`WSARecv`
// (`lpOverlapped == NULL` with an immediate success). Overlapped (asynchronous)
// completions are forwarded faithfully but their bytes are not teed in v1.
//

SOCKET WSAAPI
msocket(int af, int type, int protocol);

int WSAAPI
mconnect(SOCKET s, const sockaddr* name, int namelen);

SOCKET WSAAPI
maccept(SOCKET s, sockaddr* addr, int* addrlen);

int WSAAPI
msend(SOCKET s, const char* buf, int len, int flags);

int WSAAPI
mrecv(SOCKET s, char* buf, int len, int flags);

int WSAAPI
mclosesocket(SOCKET s);

int WSAAPI
mWSASend(SOCKET s,
         LPWSABUF lpBuffers,
         DWORD dwBufferCount,
         LPDWORD lpNumberOfBytesSent,
         DWORD dwFlags,
         LPWSAOVERLAPPED lpOverlapped,
         LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

int WSAAPI
mWSARecv(SOCKET s,
         LPWSABUF lpBuffers,
         DWORD dwBufferCount,
         LPDWORD lpNumberOfBytesRecvd,
         LPDWORD lpFlags,
         LPWSAOVERLAPPED lpOverlapped,
         LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
