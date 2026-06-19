// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace m::mwin32_impl
{
    //
    // HTTP/1.1 wire reassembly (WC-2).
    //
    // These types turn the raw byte stream teed off a socket (see the Winsock
    // shims, D20) into complete HTTP/1.1 messages. The reassembler is pure
    // logic with no transport dependency: it never references an address
    // family, a SOCKET, or any OS facility, so the same code reassembles
    // traffic captured over IPv4, IPv6, DNS-resolved, or in-process loopback
    // connections identically (D19, family-blind).
    //
    // Framing scope is HTTP/1.1 with `Content-Length` only. Chunked transfer
    // coding (`Transfer-Encoding: chunked`) is NOT decoded in v1: a message
    // that omits `Content-Length` is treated as having an empty body. This is
    // a deliberate v1 limitation matching the demo's samples, which always set
    // `Content-Length` (see DESIGN-NOTES D19).
    //

    //
    // A single HTTP header field as it appeared on the wire. The name is kept
    // verbatim (case is preserved); callers that need to match a header should
    // compare case-insensitively per RFC 9110. The value has had leading and
    // trailing optional whitespace (spaces and tabs) stripped.
    //
    struct http_header
    {
        std::string name;
        std::string value;
    };

    //
    // A fully reassembled request message.
    //
    struct http_request
    {
        std::string method;  // e.g. "GET"
        std::string target;  // request-target as sent, e.g. "/api/items?x=1"
        std::string version; // e.g. "HTTP/1.1"
        std::vector<http_header> headers;
        std::vector<std::uint8_t> body;
    };

    //
    // A fully reassembled response message.
    //
    struct http_response
    {
        std::string version; // e.g. "HTTP/1.1"
        int status_code = 0; // e.g. 200
        std::string reason;  // e.g. "OK" (may be empty)
        std::vector<http_header> headers;
        std::vector<std::uint8_t> body;
    };

    //
    // Shared framing engine. Accumulates wire bytes and yields one framed
    // message at a time: the start line plus parsed headers plus the
    // `Content-Length`-delimited body. Requests and responses frame
    // identically; only the start-line interpretation differs, which the
    // request/response reassemblers layer on top.
    //
    struct http_frame
    {
        std::string start_line;
        std::vector<http_header> headers;
        std::vector<std::uint8_t> body;
    };

    class http_framing
    {
    public:
        //
        // Append more wire bytes. A null pointer or zero length is a no-op.
        // The bytes may split a message anywhere (mid-header, mid-body) and
        // may contain several pipelined messages on a keep-alive connection.
        //
        void feed(void const* data, std::size_t length);

        //
        // Extract the next complete message, if one has fully arrived. Returns
        // true and moves it into `out` when a full header block plus its
        // `Content-Length` body are buffered; returns false (leaving `out`
        // unchanged) when more bytes are needed.
        //
        bool next(http_frame& out);

    private:
        enum class state
        {
            reading_headers,
            reading_body,
        };

        std::vector<std::uint8_t> m_buffer;
        state m_state = state::reading_headers;
        http_frame m_partial;
        std::size_t m_body_remaining = 0;
    };

    //
    // Per-connection request-stream reassembler.
    //
    class http_request_reassembler
    {
    public:
        void feed(void const* data, std::size_t length);
        bool next(http_request& out);

    private:
        http_framing m_framing;
    };

    //
    // Per-connection response-stream reassembler.
    //
    class http_response_reassembler
    {
    public:
        void feed(void const* data, std::size_t length);
        bool next(http_response& out);

    private:
        http_framing m_framing;
    };
} // namespace m::mwin32_impl
