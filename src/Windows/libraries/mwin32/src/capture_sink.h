// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <string>

#include "http_reassembler.h"

namespace m::mwin32_impl
{
    //
    // Capture sink seam (WC-3).
    //
    // The reassembler (WC-2, D21) turns teed bytes into complete HTTP/1.1
    // messages; the sink seam is the downstream consumer that receives those
    // messages and does something diagnostic with them (tally, record, or
    // validate). The seam is a pure side-channel (D6): it sits at the end of an
    // observational copy of the wire and therefore can never alter, delay, or
    // block the bytes that flow to the genuine socket. Nothing on this path
    // returns a value that the shim feeds back to the caller.
    //

    //
    // A request paired with the response that answered it. Pairing is by
    // arrival order on a single connection (FIFO), which is correct for
    // HTTP/1.1 keep-alive without pipelining — the demo's mode. A pipelined
    // connection would still pair in send order, which matches HTTP/1.1's
    // requirement that responses come back in request order.
    //
    struct http_crossing
    {
        http_request request;
        http_response response;
    };

    //
    // The seam interface. A concrete sink overrides the callbacks it cares
    // about; the defaults do nothing so a sink that only wants crossings need
    // not implement the per-message hooks.
    //
    class capture_sink
    {
    public:
        virtual ~capture_sink() = default;

        // A complete request has been reassembled.
        virtual void on_request(http_request const& /*request*/) {}

        // A complete response has been reassembled.
        virtual void on_response(http_response const& /*response*/) {}

        // A request has been paired with its response.
        virtual void on_crossing(http_crossing const& /*crossing*/) {}
    };

    //
    // A diagnostics sink that tallies what crossed the wire. Counts requests,
    // responses, and paired crossings, and keeps per-method and per-status
    // breakdowns. Holds no references to the wire and never throws into the
    // capture path.
    //
    class tallying_capture_sink final : public capture_sink
    {
    public:
        void on_request(http_request const& request) override;
        void on_response(http_response const& response) override;
        void on_crossing(http_crossing const& crossing) override;

        std::size_t request_count() const { return m_request_count; }
        std::size_t response_count() const { return m_response_count; }
        std::size_t crossing_count() const { return m_crossing_count; }

        // Number of requests seen with the given method (0 if none).
        std::size_t requests_with_method(std::string const& method) const;

        // Number of responses seen with the given status code (0 if none).
        std::size_t responses_with_status(int status_code) const;

    private:
        std::size_t m_request_count = 0;
        std::size_t m_response_count = 0;
        std::size_t m_crossing_count = 0;
        std::map<std::string, std::size_t> m_by_method;
        std::map<int, std::size_t> m_by_status;
    };

    //
    // The per-connection pump that wires the two reassembled streams to a
    // sink. The owner feeds it the bytes teed off the connection — the request
    // stream and the response stream — and the pump drains complete messages,
    // notifies the sink per message, and emits a crossing each time a request
    // can be paired (FIFO) with a response.
    //
    // The pump owns no socket and copies nothing back to the wire; it is the
    // consuming end of the observational tee.
    //
    class connection_capture
    {
    public:
        explicit connection_capture(capture_sink& sink) : m_sink(sink) {}

        // Feed bytes from the request stream (client -> server).
        void on_request_bytes(void const* data, std::size_t length);

        // Feed bytes from the response stream (server -> client).
        void on_response_bytes(void const* data, std::size_t length);

    private:
        void drain();

        capture_sink& m_sink;
        http_request_reassembler m_request_stream;
        http_response_reassembler m_response_stream;
        std::deque<http_request> m_unpaired_requests;
        std::deque<http_response> m_unpaired_responses;
    };
} // namespace m::mwin32_impl
