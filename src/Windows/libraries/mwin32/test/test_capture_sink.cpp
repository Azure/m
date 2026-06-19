// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "capture_sink.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

using m::mwin32_impl::capture_sink;
using m::mwin32_impl::connection_capture;
using m::mwin32_impl::http_crossing;
using m::mwin32_impl::http_request;
using m::mwin32_impl::http_response;
using m::mwin32_impl::recording_capture_sink;
using m::mwin32_impl::tallying_capture_sink;
using m::mwin32_impl::validating_capture_sink;

namespace
{
    void feed_request(connection_capture& c, std::string_view bytes)
    {
        c.on_request_bytes(bytes.data(), bytes.size());
    }

    void feed_response(connection_capture& c, std::string_view bytes)
    {
        c.on_response_bytes(bytes.data(), bytes.size());
    }

    // A sink that records the order and content of every callback so tests can
    // assert exactly what the seam forwarded.
    class recording_sink final : public capture_sink
    {
    public:
        std::vector<std::string> requests;   // targets
        std::vector<int> responses;          // status codes
        std::vector<std::string> crossings;  // "method target -> status"

        void on_request(http_request const& r) override
        {
            requests.push_back(r.target);
        }

        void on_response(http_response const& r) override
        {
            responses.push_back(r.status_code);
        }

        void on_crossing(http_crossing const& c) override
        {
            crossings.push_back(c.request.method + " " + c.request.target +
                                " -> " + std::to_string(c.response.status_code));
        }
    };
} // namespace

TEST(TallyingCaptureSink, CountsRequestsResponsesAndCrossings)
{
    tallying_capture_sink sink;
    connection_capture cap(sink);

    feed_request(cap, "GET /a HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "POST /b HTTP/1.1\r\nContent-Length: 2\r\n\r\nhi");
    feed_response(cap, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.request_count(), 2u);
    EXPECT_EQ(sink.response_count(), 2u);
    EXPECT_EQ(sink.crossing_count(), 2u);
}

TEST(TallyingCaptureSink, PerMethodAndPerStatusBreakdown)
{
    tallying_capture_sink sink;
    connection_capture cap(sink);

    feed_request(cap, "GET /a HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "GET /b HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "POST /c HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 500 Err\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.requests_with_method("GET"), 2u);
    EXPECT_EQ(sink.requests_with_method("POST"), 1u);
    EXPECT_EQ(sink.requests_with_method("DELETE"), 0u);
    EXPECT_EQ(sink.responses_with_status(200), 2u);
    EXPECT_EQ(sink.responses_with_status(500), 1u);
    EXPECT_EQ(sink.responses_with_status(404), 0u);
}

TEST(ConnectionCapture, PairsRequestsToResponsesInOrder)
{
    recording_sink sink;
    connection_capture cap(sink);

    feed_request(cap, "GET /one HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "GET /two HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    // Responses arrive after both requests; FIFO pairing must still hold.
    feed_response(cap, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");

    ASSERT_EQ(sink.crossings.size(), 2u);
    EXPECT_EQ(sink.crossings[0], "GET /one -> 201");
    EXPECT_EQ(sink.crossings[1], "GET /two -> 202");
}

TEST(ConnectionCapture, ResponseBeforeRequestStillPairs)
{
    // The seam does not assume the request stream drains first; a response
    // that lands before its request waits in the queue.
    recording_sink sink;
    connection_capture cap(sink);

    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_TRUE(sink.crossings.empty());
    feed_request(cap, "GET /late HTTP/1.1\r\nContent-Length: 0\r\n\r\n");

    ASSERT_EQ(sink.crossings.size(), 1u);
    EXPECT_EQ(sink.crossings[0], "GET /late -> 200");
}

TEST(ConnectionCapture, SplitFeedsAreReassembledThroughTheSeam)
{
    recording_sink sink;
    connection_capture cap(sink);

    std::string const request =
        "POST /split HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    for (char const c : request)
    {
        cap.on_request_bytes(&c, 1);
    }
    EXPECT_EQ(sink.requests.size(), 1u);
    EXPECT_EQ(sink.requests[0], "/split");
}

TEST(ConnectionCapture, MessageHooksFireBeforeCrossing)
{
    // on_request / on_response are observed as messages complete; the crossing
    // is only emitted once both sides of the pair exist.
    recording_sink sink;
    connection_capture cap(sink);

    feed_request(cap, "GET /x HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    EXPECT_EQ(sink.requests.size(), 1u);
    EXPECT_TRUE(sink.responses.empty());
    EXPECT_TRUE(sink.crossings.empty());

    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    EXPECT_EQ(sink.responses.size(), 1u);
    EXPECT_EQ(sink.crossings.size(), 1u);
}

// D6: the sink is a pure side-channel and cannot alter the bytes that flow to
// the genuine socket. This test models the shim's tee: the "wire" receives the
// genuine bytes while an identical copy is teed into the capture seam. The wire
// must be byte-identical regardless of which sink is attached.
namespace
{
    // Simulate sending `bytes` on a connection: the genuine path appends to
    // `wire`; the observational tee feeds the capture seam.
    void tee_send_request(std::vector<std::uint8_t>& wire,
                          connection_capture& cap, std::string_view bytes)
    {
        wire.insert(wire.end(), bytes.begin(), bytes.end());
        cap.on_request_bytes(bytes.data(), bytes.size());
    }

    void tee_send_response(std::vector<std::uint8_t>& wire,
                           connection_capture& cap, std::string_view bytes)
    {
        wire.insert(wire.end(), bytes.begin(), bytes.end());
        cap.on_response_bytes(bytes.data(), bytes.size());
    }

    std::vector<std::uint8_t> run_exchange(capture_sink& sink)
    {
        std::vector<std::uint8_t> wire;
        connection_capture cap(sink);
        tee_send_request(
            wire, cap, "GET /a HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
        tee_send_response(
            wire, cap, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
        return wire;
    }
} // namespace

TEST(ConnectionCapture, ByteForwardingIsUnaffectedBySink)
{
    // A no-op sink and a tallying sink must produce an identical wire.
    capture_sink null_sink;
    std::vector<std::uint8_t> const wire_null = run_exchange(null_sink);

    tallying_capture_sink tally;
    std::vector<std::uint8_t> const wire_tally = run_exchange(tally);

    EXPECT_EQ(wire_null, wire_tally);

    // And the wire equals exactly the bytes that were sent.
    std::string const expected =
        "GET /a HTTP/1.1\r\nContent-Length: 0\r\n\r\n"
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    std::vector<std::uint8_t> const expected_bytes(expected.begin(),
                                                   expected.end());
    EXPECT_EQ(wire_tally, expected_bytes);

    // The capture still observed the exchange.
    EXPECT_EQ(tally.request_count(), 1u);
    EXPECT_EQ(tally.response_count(), 1u);
    EXPECT_EQ(tally.crossing_count(), 1u);
}

// ---------------------------------------------------------------------------
// WC-5: record mode wires the seam to the PIL contract recorder.
// ---------------------------------------------------------------------------

TEST(RecordingCaptureSink, FeedsObservedCrossingsToRecorder)
{
    auto recorder = m::pil::make_http_contract_recorder();
    recording_capture_sink sink(*recorder);
    connection_capture cap(sink);

    feed_request(cap, "GET /a HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "POST /items HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");

    // Two distinct (method, path) operations were observed.
    EXPECT_EQ(recorder->operation_count(), 2u);
}

TEST(RecordingCaptureSink, EmittedSpecNamesObservedPaths)
{
    auto recorder = m::pil::make_http_contract_recorder();
    recording_capture_sink sink(*recorder);
    connection_capture cap(sink);

    feed_request(cap, "GET /widgets HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    std::string const spec = recorder->emit_spec();
    EXPECT_NE(spec.find("openapi"), std::string::npos);
    EXPECT_NE(spec.find("/widgets"), std::string::npos);
}

TEST(RecordingCaptureSink, QueryStringIsStrippedFromOperationPath)
{
    auto recorder = m::pil::make_http_contract_recorder();
    recording_capture_sink sink(*recorder);
    connection_capture cap(sink);

    // Same path, two different query strings: one operation, not two.
    feed_request(cap, "GET /search?q=a HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "GET /search?q=b HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(recorder->operation_count(), 1u);
}

// ---------------------------------------------------------------------------
// WC-5: validate mode runs each crossing through a loaded contract document
// and tallies violations per direction. A fake document stands in for the PIL
// validating provider so the sink's tallying logic is what is under test.
// ---------------------------------------------------------------------------

namespace
{
    // A fake contract document with configurable verdicts. A request whose path
    // contains "/bad" is a request-direction violation; a response with status
    // >= 500 is a response-direction violation. When `fail_with_ec` is set, both
    // validations report an operational error instead of a verdict.
    class fake_document final : public m::pil::ihttp_contract_document
    {
    public:
        bool fail_with_ec = false;
        std::size_t request_calls = 0;
        std::size_t response_calls = 0;

        validate_request_disposition
        validate_request(std::string_view              /*method*/,
                         std::string_view              path,
                         std::span<m::pil::http_header const> /*headers*/,
                         std::span<std::uint8_t const> /*body*/,
                         std::error_code&              ec) override
        {
            ++request_calls;
            if (fail_with_ec)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return {};
            }
            ec.clear();
            if (path.find("/bad") != std::string_view::npos)
                return validate_request_disposition(
                    validate_request_result_code::parameter_invalid);
            return {};
        }

        validate_response_disposition
        validate_response(std::string_view              /*method*/,
                          std::string_view              /*path*/,
                          std::uint16_t                 status,
                          std::span<m::pil::http_header const> /*headers*/,
                          std::span<std::uint8_t const> /*body*/,
                          std::error_code&              ec) override
        {
            ++response_calls;
            if (fail_with_ec)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return {};
            }
            ec.clear();
            if (status >= 500)
                return validate_response_disposition(
                    validate_response_result_code::undeclared_status);
            return {};
        }
    };
} // namespace

TEST(ValidatingCaptureSink, ConformingCrossingHasNoViolations)
{
    fake_document document;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "GET /ok HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.tally().requests_checked, 1u);
    EXPECT_EQ(sink.tally().request_violations, 0u);
    EXPECT_EQ(sink.tally().responses_checked, 1u);
    EXPECT_EQ(sink.tally().response_violations, 0u);
}

TEST(ValidatingCaptureSink, BadRequestCountedAsRequestViolation)
{
    fake_document document;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "POST /bad HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.tally().requests_checked, 1u);
    EXPECT_EQ(sink.tally().request_violations, 1u);
    EXPECT_EQ(sink.tally().responses_checked, 1u);
    EXPECT_EQ(sink.tally().response_violations, 0u);
}

TEST(ValidatingCaptureSink, BadResponseCountedAsResponseViolation)
{
    fake_document document;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "GET /ok HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 500 Server Error\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.tally().requests_checked, 1u);
    EXPECT_EQ(sink.tally().request_violations, 0u);
    EXPECT_EQ(sink.tally().responses_checked, 1u);
    EXPECT_EQ(sink.tally().response_violations, 1u);
}

TEST(ValidatingCaptureSink, ViolationsCountIndependentlyPerDirection)
{
    // A single crossing can violate in both directions; each is tallied once.
    fake_document document;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "POST /bad HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.tally().request_violations, 1u);
    EXPECT_EQ(sink.tally().response_violations, 1u);
}

TEST(ValidatingCaptureSink, TalliesAcrossMultipleCrossings)
{
    fake_document document;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "GET /ok HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "GET /bad HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    feed_request(cap, "GET /ok HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 500 Err\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(sink.tally().requests_checked, 3u);
    EXPECT_EQ(sink.tally().request_violations, 1u);
    EXPECT_EQ(sink.tally().responses_checked, 3u);
    EXPECT_EQ(sink.tally().response_violations, 1u);
}

TEST(ValidatingCaptureSink, OperationalErrorIsNotCounted)
{
    // A malformed-spec error (reported through the error_code channel) is
    // neither a check nor a violation in either direction.
    fake_document document;
    document.fail_with_ec = true;
    validating_capture_sink sink(document);
    connection_capture cap(sink);

    feed_request(cap, "GET /bad HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    feed_response(cap, "HTTP/1.1 500 Err\r\nContent-Length: 0\r\n\r\n");

    // The document was consulted, but no clean verdict was tallied.
    EXPECT_GT(document.request_calls, 0u);
    EXPECT_GT(document.response_calls, 0u);
    EXPECT_EQ(sink.tally().requests_checked, 0u);
    EXPECT_EQ(sink.tally().request_violations, 0u);
    EXPECT_EQ(sink.tally().responses_checked, 0u);
    EXPECT_EQ(sink.tally().response_violations, 0u);
}

TEST(ValidatingCaptureSink, DoesNotAlterTheWire)
{
    // D6: the validating sink reads the observational copy only; with a sink
    // attached the wire is byte-identical to the no-op run.
    fake_document document;
    validating_capture_sink sink(document);
    std::vector<std::uint8_t> const wire_validate = run_exchange(sink);

    capture_sink null_sink;
    std::vector<std::uint8_t> const wire_null = run_exchange(null_sink);

    EXPECT_EQ(wire_validate, wire_null);
}

