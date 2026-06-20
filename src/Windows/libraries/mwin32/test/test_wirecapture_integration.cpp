// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// test_wirecapture_integration.cpp — end-to-end integration coverage for the
// wire-capture demo (M-WIRECAP-INTEG / WC-9, WC-10, WC-11).
//
// The reusable harness (wirecapture_harness.h) runs a tiny HTTP/1.1 server and
// client over a real loopback socket (or the in-process synthetic edge) and
// returns the request/response byte streams teed off the connection. These tests
// replay those streams through a `connection_capture` into the WC-5 sinks to
// exercise the two halves of the demo lifecycle:
//
//   * derive  — clean traffic + a recording sink yields an OpenAPI YAML that
//     describes both endpoints/statuses and loads cleanly (WC-9, phase 1).
//   * detect  — the derived YAML, loaded in validate mode, flags injected
//     request- and response-direction violations while the connection stays up
//     (WC-10, phase 2).
//   * matrix  — the derive -> detect lifecycle is transport-independent: the
//     derived spec and the violation tallies are equal across IPv4, IPv6, DNS,
//     and the synthetic edge (WC-11).
//

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <m/pil/http_contract_interfaces.h>
#include <m/pil/http_contract_recorder.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>

#include "capture_sink.h"
#include "wirecapture_harness.h"

#include <gtest/gtest.h>

using namespace std::string_view_literals;

using m::mwin32_impl::connection_capture;
using m::mwin32_impl::recording_capture_sink;
using m::mwin32_impl::validating_capture_sink;
using m::mwin32_impl::validation_tally;
using m::mwin32_impl::wirecapture_test::captured_streams;
using m::mwin32_impl::wirecapture_test::harness_options;
using m::mwin32_impl::wirecapture_test::harness_transport;
using m::mwin32_impl::wirecapture_test::wire_capture_harness;

namespace
{
    // Replays a captured exchange through a connection_capture into `sink`. The
    // request stream and response stream are fed in FIFO order so the pump pairs
    // each request with its response.
    void
    replay(captured_streams const& streams, m::mwin32_impl::capture_sink& sink)
    {
        connection_capture pump(sink);
        pump.on_request_bytes(streams.requests.data(), streams.requests.size());
        pump.on_response_bytes(streams.responses.data(), streams.responses.size());
    }

    // Runs the harness over `transport` with the given fault knobs, returning the
    // captured streams. Fails the calling test on a harness setup error.
    captured_streams
    run_exchange(harness_transport transport, bool fault_request, bool fault_response)
    {
        harness_options opts;
        opts.transport      = transport;
        opts.fault_request  = fault_request;
        opts.fault_response = fault_response;

        wire_capture_harness harness(opts);
        EXPECT_TRUE(harness.run()) << harness.error();
        return harness.captured();
    }

    // Derives an OpenAPI spec from a clean exchange over `transport` and returns
    // the emitted YAML together with the recorder's operation count.
    struct derived_spec
    {
        std::string emitted;
        std::size_t operation_count = 0;
    };

    derived_spec
    derive_over(harness_transport transport)
    {
        captured_streams const streams = run_exchange(transport, false, false);

        auto recorder = m::pil::make_http_contract_recorder();
        recording_capture_sink sink(*recorder);
        replay(streams, sink);

        derived_spec out;
        out.emitted         = recorder->emit_spec();
        out.operation_count = recorder->operation_count();
        return out;
    }

    // Loads spec bytes through the live PIL contract provider.
    std::unique_ptr<m::pil::ihttp_contract_document>
    load_contract(std::string_view spec)
    {
        auto platform = m::pil::make_platform_interface();
        return platform->get_http_contract()->load(spec);
    }
} // namespace

//
// ---- WC-9: derive phase ----------------------------------------------------
//

//
// Over IPv4 loopback, a clean exchange recorded in "record" mode yields a
// non-empty OpenAPI YAML describing exactly the two observed operations, and
// that derived spec loads cleanly through the live contract provider.
//
TEST(WireCaptureDerive, CleanIpv4TrafficYieldsLoadableSpec)
{
    derived_spec const derived = derive_over(harness_transport::ipv4);

    EXPECT_EQ(derived.operation_count, 2u);
    ASSERT_FALSE(derived.emitted.empty());

    // The derived document names both endpoints and both observed statuses.
    EXPECT_NE(derived.emitted.find("/health"), std::string::npos);
    EXPECT_NE(derived.emitted.find("/widgets"), std::string::npos);
    EXPECT_NE(derived.emitted.find("200"), std::string::npos);
    EXPECT_NE(derived.emitted.find("201"), std::string::npos);

    // The derived spec loads cleanly and describes two synthesizable operations.
    auto document = load_contract(derived.emitted);
    ASSERT_NE(document, nullptr);

    auto const synthesized = document->synthesize_requests();
    ASSERT_EQ(synthesized.size(), 2u);

    bool saw_health  = false;
    bool saw_widgets = false;
    for (auto const& request : synthesized)
    {
        if (request.path == "/health"sv)
            saw_health = true;
        if (request.path == "/widgets"sv)
            saw_widgets = true;
    }
    EXPECT_TRUE(saw_health);
    EXPECT_TRUE(saw_widgets);
}

//
// The synthetic edge (no Winsock) derives the identical spec, confirming the
// recorder keys off message content, not transport (a WC-11 precondition proved
// early here so the derive phase is self-contained).
//
TEST(WireCaptureDerive, SyntheticEdgeMatchesIpv4Spec)
{
    derived_spec const over_ipv4      = derive_over(harness_transport::ipv4);
    derived_spec const over_synthetic = derive_over(harness_transport::synthetic);

    EXPECT_EQ(over_synthetic.operation_count, over_ipv4.operation_count);
    EXPECT_EQ(over_synthetic.emitted, over_ipv4.emitted);
}

//
// ---- WC-10: detect phase ---------------------------------------------------
//

//
// Over IPv4 loopback, the *derived* contract loaded in validate mode catches a
// fault injected in each direction — a non-conforming request body (client ->
// server) and a non-conforming response body (server -> client) — while both
// the request and the response still complete (the connection is never broken).
//
TEST(WireCaptureDetect, FaultsInBothDirectionsAreCaughtWhileTrafficCompletes)
{
    // Phase 1: derive a clean contract to validate against.
    derived_spec const clean = derive_over(harness_transport::ipv4);
    ASSERT_FALSE(clean.emitted.empty());

    // Keep the platform alive for the lifetime of the loaded document.
    auto platform = m::pil::make_platform_interface();
    auto document = platform->get_http_contract()->load(clean.emitted);
    ASSERT_NE(document, nullptr);

    // Phase 2: run a faulted exchange in BOTH directions over the same transport.
    captured_streams const streams =
        run_exchange(harness_transport::ipv4, /*fault_request=*/true, /*fault_response=*/true);

    // The connection completed: both the request and response streams carry the
    // two framed messages of the exchange (the faults are contract violations,
    // not transport faults, so nothing is dropped).
    ASSERT_FALSE(streams.requests.empty());
    ASSERT_FALSE(streams.responses.empty());

    validating_capture_sink sink(*document);
    replay(streams, sink);

    auto const& tally = sink.tally();
    EXPECT_EQ(tally.requests_checked, 2u);
    EXPECT_EQ(tally.responses_checked, 2u);
    EXPECT_GE(tally.request_violations, 1u);
    EXPECT_GE(tally.response_violations, 1u);
}

//
// Validate mode is quiet on conforming traffic: the same derived contract run
// against a clean exchange records no violations (the false-positive guard that
// makes the fault detection above meaningful).
//
TEST(WireCaptureDetect, CleanTrafficYieldsNoViolations)
{
    derived_spec const clean = derive_over(harness_transport::ipv4);
    ASSERT_FALSE(clean.emitted.empty());

    auto platform = m::pil::make_platform_interface();
    auto document = platform->get_http_contract()->load(clean.emitted);
    ASSERT_NE(document, nullptr);

    captured_streams const streams =
        run_exchange(harness_transport::ipv4, /*fault_request=*/false, /*fault_response=*/false);

    validating_capture_sink sink(*document);
    replay(streams, sink);

    auto const& tally = sink.tally();
    EXPECT_EQ(tally.requests_checked, 2u);
    EXPECT_EQ(tally.responses_checked, 2u);
    EXPECT_EQ(tally.request_violations, 0u);
    EXPECT_EQ(tally.response_violations, 0u);
}

//
// ---- WC-11: transport matrix -----------------------------------------------
//

namespace
{
    // The full derive -> detect lifecycle over one transport: derive a clean
    // contract, load it, then validate a faulted exchange against it. Returns the
    // derived spec and the resulting violation tally so the matrix test can assert
    // they are equal across every transport.
    struct lifecycle_result
    {
        std::string      derived;
        validation_tally tally;
    };

    lifecycle_result
    run_lifecycle(harness_transport transport)
    {
        // Derive from clean traffic.
        captured_streams const clean = run_exchange(transport, false, false);
        auto                   recorder = m::pil::make_http_contract_recorder();
        recording_capture_sink record_sink(*recorder);
        replay(clean, record_sink);

        lifecycle_result result;
        result.derived = recorder->emit_spec();

        // Load the derived contract through the live provider, keeping the
        // platform alive for the validation below.
        auto platform = m::pil::make_platform_interface();
        auto document = platform->get_http_contract()->load(result.derived);
        EXPECT_NE(document, nullptr);
        if (document == nullptr)
            return result;

        // Detect against a faulted exchange over the same transport.
        captured_streams const faulted = run_exchange(transport, true, true);
        validating_capture_sink validate_sink(*document);
        replay(faulted, validate_sink);
        result.tally = validate_sink.tally();
        return result;
    }

    bool
    tallies_equal(validation_tally const& a, validation_tally const& b)
    {
        return a.requests_checked == b.requests_checked &&
               a.request_violations == b.request_violations &&
               a.responses_checked == b.responses_checked &&
               a.response_violations == b.response_violations;
    }
} // namespace

//
// The derive -> detect lifecycle is transport-independent: run over IPv4, IPv6,
// a DNS-resolved loopback name, and the in-process synthetic edge, the derived
// spec and the violation tallies are identical. This is the headline result of
// the demo — capture is a property of the byte stream, not the transport (D19).
//
TEST(WireCaptureMatrix, DeriveAndDetectAreEquivalentAcrossTransports)
{
    lifecycle_result const ipv4      = run_lifecycle(harness_transport::ipv4);
    lifecycle_result const ipv6      = run_lifecycle(harness_transport::ipv6);
    lifecycle_result const dns       = run_lifecycle(harness_transport::dns);
    lifecycle_result const synthetic = run_lifecycle(harness_transport::synthetic);

    // The derived spec is byte-identical across every transport.
    EXPECT_FALSE(ipv4.derived.empty());
    EXPECT_EQ(ipv6.derived, ipv4.derived);
    EXPECT_EQ(dns.derived, ipv4.derived);
    EXPECT_EQ(synthetic.derived, ipv4.derived);

    // The violation tallies match across every transport.
    EXPECT_TRUE(tallies_equal(ipv6.tally, ipv4.tally));
    EXPECT_TRUE(tallies_equal(dns.tally, ipv4.tally));
    EXPECT_TRUE(tallies_equal(synthetic.tally, ipv4.tally));

    // And the shared tally is the non-trivial one: a violation in each direction
    // (so "equivalent" is not merely "all zero").
    EXPECT_EQ(ipv4.tally.requests_checked, 2u);
    EXPECT_EQ(ipv4.tally.responses_checked, 2u);
    EXPECT_GE(ipv4.tally.request_violations, 1u);
    EXPECT_GE(ipv4.tally.response_violations, 1u);
}
