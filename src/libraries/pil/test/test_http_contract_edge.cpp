// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Tests for the public contract-edge seam (M-HWC-CONTRACT-EDGE, D-HWC-10). The
// edge ties attached validate-mode documents to one pluggable engine: every
// request/response crossing it is validated and tallied, the engine's response
// is never altered, and drive traffic crosses the same seam via
// as_engine_submit(). Specs are inline YAML; the "engine" is a fake callable.
//

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "contract/http_contract_provider.h"

#include <m/pil/http_contract_edge.h>
#include <m/pil/http_contract_interfaces.h>

namespace
{
    using m::pil::captured_contract_response;
    using m::pil::contract_edge_tally;
    using m::pil::drive_contract;
    using m::pil::drive_tally;
    using m::pil::http_header;
    using m::pil::ihttp_contract_document;
    using m::pil::ihttp_contract_edge;
    using m::pil::make_contract_edge;
    using m::pil::make_http_contract_provider;
    using m::pil::synthesized_request;

    //
    // A minimal OAS 3.0 spec: GET /ping returns a JSON 200 whose body must carry
    // a boolean `pong`. Only the 200 status is declared.
    //
    constexpr std::string_view kSpec = R"(
openapi: 3.0.0
info:
  title: edge-test
  version: "1.0"
paths:
  /ping:
    get:
      responses:
        '200':
          description: ok
          content:
            application/json:
              schema:
                type: object
                required: [pong]
                properties:
                  pong: { type: boolean }
)";

    std::shared_ptr<ihttp_contract_document>
    load_document()
    {
        auto provider = make_http_contract_provider();
        auto doc      = provider->load(kSpec);
        EXPECT_NE(doc, nullptr);
        return std::shared_ptr<ihttp_contract_document>(std::move(doc));
    }

    std::vector<std::uint8_t>
    bytes(std::string_view s)
    {
        return {s.begin(), s.end()};
    }

    //
    // A response with the given status and JSON body (Content-Type set).
    //
    captured_contract_response
    json_response(std::uint16_t status, std::string_view body)
    {
        captured_contract_response response;
        response.status = status;
        response.headers.emplace_back("Content-Type", "application/json");
        response.body = bytes(body);
        return response;
    }

    //
    // The synthesized GET /ping request (a conforming request by construction).
    //
    synthesized_request
    ping_request()
    {
        auto const reqs = load_document()->synthesize_requests();
        EXPECT_FALSE(reqs.empty());
        return reqs.front();
    }

    //------------------------------------------------------------------------

    TEST(ContractEdge, PassesTrafficThroughWhenNoDocumentsAttached)
    {
        auto const expected = json_response(200, R"({"pong":true})");
        auto       edge     = make_contract_edge([&](synthesized_request const&) { return expected; });

        auto const got = edge->submit(ping_request());

        EXPECT_EQ(got.status, 200u);
        EXPECT_EQ(got.body, expected.body);

        auto const t = edge->tally();
        EXPECT_EQ(t.requests, 1u);
        EXPECT_EQ(t.responses, 1u);
        EXPECT_EQ(t.request_violations, 0u);
        EXPECT_EQ(t.response_violations, 0u);
    }

    TEST(ContractEdge, ConformingCrossingTalliesNoViolation)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(200, R"({"pong":true})"); });
        edge->attach_validation(load_document());

        edge->submit(ping_request());

        auto const t = edge->tally();
        EXPECT_EQ(t.request_violations, 0u);
        EXPECT_EQ(t.response_violations, 0u);
    }

    TEST(ContractEdge, UndeclaredStatusTalliesResponseViolation)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(500, R"({"pong":true})"); });
        edge->attach_validation(load_document());

        edge->submit(ping_request());

        auto const t = edge->tally();
        EXPECT_EQ(t.response_violations, 1u);
        EXPECT_EQ(t.request_violations, 0u);
    }

    TEST(ContractEdge, BodySchemaInvalidTalliesResponseViolation)
    {
        // 200 is declared but the body is missing the required `pong`.
        auto edge = make_contract_edge([](synthesized_request const&) { return json_response(200, R"({})"); });
        edge->attach_validation(load_document());

        edge->submit(ping_request());

        EXPECT_EQ(edge->tally().response_violations, 1u);
    }

    TEST(ContractEdge, UnknownRequestTalliesRequestViolation)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(200, R"({"pong":true})"); });
        edge->attach_validation(load_document());

        synthesized_request unknown;
        unknown.method = "GET";
        unknown.path   = "/nope";
        edge->submit(unknown);

        EXPECT_EQ(edge->tally().request_violations, 1u);
    }

    TEST(ContractEdge, ViolationDoesNotAlterEngineResponse)
    {
        auto const engine_response = json_response(500, R"({"detail":"boom"})");
        auto       edge = make_contract_edge([&](synthesized_request const&) { return engine_response; });
        edge->attach_validation(load_document());

        auto const got = edge->submit(ping_request());

        // The contract is violated (500 undeclared) but the engine's response is
        // returned verbatim — D6, validate never alters behavior.
        EXPECT_EQ(got.status, 500u);
        EXPECT_EQ(got.body, engine_response.body);
        EXPECT_EQ(edge->tally().response_violations, 1u);
    }

    TEST(ContractEdge, DriveContractCrossesEdgeAndValidates)
    {
        // The drive document validates the response itself; a separate attached
        // validate document observes the same crossing through the edge.
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(500, R"({"pong":true})"); });
        edge->attach_validation(load_document());

        auto const drive_doc = load_document();
        auto const tally     = drive_contract(*drive_doc, edge->as_engine_submit());

        EXPECT_EQ(tally.requests, 1u);
        EXPECT_EQ(tally.violating, 1u); // drive doc saw the undeclared 500
        EXPECT_EQ(edge->tally().requests, 1u);
        EXPECT_EQ(edge->tally().response_violations, 1u); // attached doc saw it too
    }

    TEST(ContractEdge, MultipleAttachedDocumentsEachCount)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(500, R"({"pong":true})"); });
        edge->attach_validation(load_document());
        edge->attach_validation(load_document());

        edge->submit(ping_request());

        EXPECT_EQ(edge->tally().response_violations, 2u);
    }

    TEST(ContractEdge, TallyAccumulatesAcrossSubmits)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(200, R"({"pong":true})"); });

        edge->submit(ping_request());
        edge->submit(ping_request());
        edge->submit(ping_request());

        auto const t = edge->tally();
        EXPECT_EQ(t.requests, 3u);
        EXPECT_EQ(t.responses, 3u);
    }

    TEST(ContractEdge, AsEngineSubmitForwardsToSubmit)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(200, R"({"pong":true})"); });

        auto const submit = edge->as_engine_submit();
        auto const got    = submit(ping_request());

        EXPECT_EQ(got.status, 200u);
        EXPECT_EQ(edge->tally().requests, 1u);
    }

    TEST(ContractEdge, NullEngineYieldsEmptyResponse)
    {
        auto edge = make_contract_edge(nullptr);

        auto const got = edge->submit(ping_request());

        EXPECT_EQ(got.status, 0u);
        EXPECT_EQ(edge->tally().requests, 1u);
        EXPECT_EQ(edge->tally().responses, 1u);
    }

    TEST(ContractEdge, ConformingRequestNoRequestViolation)
    {
        auto edge =
            make_contract_edge([](synthesized_request const&) { return json_response(200, R"({"pong":true})"); });
        edge->attach_validation(load_document());

        edge->submit(ping_request());

        EXPECT_EQ(edge->tally().request_violations, 0u);
    }
} // namespace
