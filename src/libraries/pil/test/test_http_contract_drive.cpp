// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Tests for drive mode: the example/default request synthesizer, the
// engine-agnostic driver, and the drive+validate integration
// (M-HWC-CONTRACT-DRIVE, D-HWC-8). Specs are inline YAML; the "engine" is a
// fake submit callable. No file I/O.
//

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "contract/contract_example_driver.h"
#include "contract/http_contract_provider.h"
#include "contract/openapi_model.h"

#include <m/pil/http_contract_interfaces.h>

namespace
{
    using m::pil::captured_contract_response;
    using m::pil::drive_contract;
    using m::pil::drive_tally;
    using m::pil::http_header;
    using m::pil::ihttp_contract_document;
    using m::pil::load_openapi_model;
    using m::pil::make_http_contract_provider;
    using m::pil::synthesize_contract_requests;
    using m::pil::synthesized_request;

    std::vector<synthesized_request>
    synthesize(std::string_view spec)
    {
        return synthesize_contract_requests(load_openapi_model(spec));
    }

    synthesized_request const*
    find_request(std::vector<synthesized_request> const& reqs,
                 std::string_view                        method,
                 std::string_view                        path_contains)
    {
        for (auto const& r : reqs)
            if (r.method == method && r.path.find(path_contains) != std::string::npos)
                return &r;
        return nullptr;
    }

    std::optional<std::string>
    header_value(synthesized_request const& r, std::string_view name)
    {
        for (auto const& [k, v] : r.headers)
            if (k == name)
                return v;
        return std::nullopt;
    }

    std::string
    body_text(synthesized_request const& r)
    {
        return std::string(r.body.begin(), r.body.end());
    }

    //------------------------------------------------------------------------
    // DRIVE-1: synthesizer
    //------------------------------------------------------------------------

    TEST(ContractSynthesize, OneRequestPerOperation)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /a: { get: { responses: { '200': { description: ok } } } }
  /b: { get: { responses: { '200': { description: ok } } } }
  /c: { post: { responses: { '201': { description: ok } } } }
)";
        auto const reqs = synthesize(spec);
        EXPECT_EQ(reqs.size(), 3u);
    }

    TEST(ContractSynthesize, PathTemplateFilledFromExample)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /items/{id}:
    get:
      parameters:
        - { name: id, in: path, required: true, schema: { type: string }, example: "42" }
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(reqs[0].path, "/items/42");
    }

    TEST(ContractSynthesize, PathFilledFromSchemaDefaultWhenNoExample)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /items/{id}:
    get:
      parameters:
        - { name: id, in: path, required: true, schema: { type: integer } }
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(reqs[0].path, "/items/0");
    }

    TEST(ContractSynthesize, RequiredQueryParameterAppended)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /search:
    get:
      parameters:
        - { name: q, in: query, required: true, schema: { type: string }, example: "term" }
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(reqs[0].path, "/search?q=term");
    }

    TEST(ContractSynthesize, QueryDiscriminatorAppendedWithFixedValue)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  "/machine?comp=package":
    get:
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(reqs[0].path, "/machine?comp=package");
    }

    TEST(ContractSynthesize, HeaderParameterEmitted)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /ping:
    get:
      parameters:
        - { name: X-Token, in: header, required: true, schema: { type: string }, example: "tok" }
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(header_value(reqs[0], "X-Token"), "tok");
    }

    TEST(ContractSynthesize, JsonRequestBodyFromExample)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /widgets:
    post:
      requestBody:
        required: true
        content:
          application/json:
            schema: { type: object, required: [size], properties: { size: { type: integer } } }
            example: { size: 7 }
      responses: { '201': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(header_value(reqs[0], "Content-Type"), "application/json");
        EXPECT_NE(body_text(reqs[0]).find("\"size\":7"), std::string::npos);
    }

    TEST(ContractSynthesize, JsonRequestBodyFromSchemaDefault)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /widgets:
    post:
      requestBody:
        required: true
        content:
          application/json:
            schema: { type: object, required: [size], properties: { size: { type: integer } } }
      responses: { '201': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(header_value(reqs[0], "Content-Type"), "application/json");
        // A schema default fills the required `size` with 0.
        EXPECT_NE(body_text(reqs[0]).find("\"size\":0"), std::string::npos);
    }

    TEST(ContractSynthesize, NonJsonBodyExampleUsed)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /report:
    post:
      requestBody:
        content:
          text/xml:
            example: "<r/>"
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(header_value(reqs[0], "Content-Type"), "text/xml");
        EXPECT_EQ(body_text(reqs[0]), "<r/>");
    }

    TEST(ContractSynthesize, BareOperationProducesMethodAndPathOnly)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info: { title: t, version: "1.0" }
paths:
  /health:
    get:
      responses: { '200': { description: ok } }
)";
        auto const reqs = synthesize(spec);
        ASSERT_EQ(reqs.size(), 1u);
        EXPECT_EQ(reqs[0].method, "GET");
        EXPECT_EQ(reqs[0].path, "/health");
        EXPECT_TRUE(reqs[0].headers.empty());
        EXPECT_TRUE(reqs[0].body.empty());
    }

    //------------------------------------------------------------------------
    // DRIVE-2: driver
    //------------------------------------------------------------------------

    constexpr std::string_view kDriveSpec = R"(
openapi: 3.0.0
info: { title: drive, version: "1.0" }
paths:
  /items/{id}:
    get:
      parameters:
        - { name: id, in: path, required: true, schema: { type: string }, example: "42" }
        - { name: verbose, in: query, required: true, schema: { type: string }, example: "yes" }
        - { name: X-Token, in: header, required: true, schema: { type: string }, example: "tok" }
      responses:
        '200':
          description: ok
          headers: { X-Trace: { required: true, schema: { type: string } } }
          content:
            application/json:
              schema: { type: object, required: [name], properties: { name: { type: string } } }
  /widgets:
    post:
      requestBody:
        required: true
        content:
          application/json:
            schema: { type: object, required: [size], properties: { size: { type: integer } } }
            example: { size: 7 }
      responses: { '201': { description: created } }
)";

    std::vector<std::uint8_t>
    bytes(std::string_view s)
    {
        return {s.begin(), s.end()};
    }

    TEST(ContractDrive, SubmitsEveryRequestWithoutValidator)
    {
        auto const  reqs      = synthesize(kDriveSpec);
        std::size_t submitted = 0;
        auto const  tally     = drive_contract(
            reqs,
            [&](synthesized_request const&) -> captured_contract_response {
                ++submitted;
                return {200, {}, {}};
            },
            nullptr);

        EXPECT_EQ(submitted, reqs.size());
        EXPECT_EQ(tally.requests, reqs.size());
        EXPECT_EQ(tally.responses_validated, 0u);
    }

    TEST(ContractDrive, TalliesConformingResponses)
    {
        auto       provider = make_http_contract_provider();
        auto       doc      = provider->load(kDriveSpec);
        auto const reqs     = synthesize(kDriveSpec);

        auto const tally = drive_contract(
            reqs,
            [&](synthesized_request const& req) -> captured_contract_response {
                if (req.method == "GET")
                    return {200,
                            {{"X-Trace", "tr"}, {"Content-Type", "application/json"}},
                            bytes(R"({"name":"n"})")};
                return {201, {}, {}}; // POST /widgets: declared, no body
            },
            doc.get());

        EXPECT_EQ(tally.responses_validated, reqs.size());
        EXPECT_EQ(tally.conforming, reqs.size());
        EXPECT_EQ(tally.violating, 0u);
    }

    TEST(ContractDrive, TalliesViolatingResponses)
    {
        auto       provider = make_http_contract_provider();
        auto       doc      = provider->load(kDriveSpec);
        auto const reqs     = synthesize(kDriveSpec);

        auto const tally = drive_contract(
            reqs,
            [&](synthesized_request const& req) -> captured_contract_response {
                if (req.method == "GET")
                    // Missing required X-Trace header -> response violation.
                    return {200,
                            {{"Content-Type", "application/json"}},
                            bytes(R"({"name":"n"})")};
                return {500, {}, {}}; // POST: undeclared status -> violation
            },
            doc.get());

        EXPECT_EQ(tally.responses_validated, reqs.size());
        EXPECT_EQ(tally.conforming, 0u);
        EXPECT_EQ(tally.violating, reqs.size());
    }

    //------------------------------------------------------------------------
    // DRIVE-3: drive + validate integration over the (fake) engine
    //------------------------------------------------------------------------

    TEST(ContractDriveIntegration, ExamplesDriveFakeEngineAndResponsesValidated)
    {
        auto provider = make_http_contract_provider();
        std::shared_ptr<ihttp_contract_document> doc = provider->load(kDriveSpec);
        ASSERT_NE(doc, nullptr);

        // Every operation in the spec yields exactly one synthesized request.
        auto const reqs = synthesize(kDriveSpec);
        ASSERT_EQ(reqs.size(), 2u);
        ASSERT_NE(find_request(reqs, "GET", "/items/42"), nullptr);
        ASSERT_NE(find_request(reqs, "POST", "/widgets"), nullptr);

        // The synthesized requests are themselves contract-conforming.
        for (auto const& req : reqs)
        {
            std::error_code ec;
            auto const      d = doc->validate_request(req.method, req.path, req.headers, req.body, ec);
            EXPECT_FALSE(ec);
            EXPECT_FALSE(d) << "synthesized " << req.method << " " << req.path;
        }

        // A fake engine answers each operation with a conforming response; drive
        // composes with validate and every captured response is contract-checked.
        std::size_t served = 0;
        auto const  tally  = drive_contract(
            reqs,
            [&](synthesized_request const& req) -> captured_contract_response {
                ++served;
                if (req.method == "GET")
                    return {200,
                            {{"X-Trace", "tr"}, {"Content-Type", "application/json"}},
                            bytes(R"({"name":"widget"})")};
                return {201, {}, {}};
            },
            doc.get());

        EXPECT_EQ(served, reqs.size());
        EXPECT_EQ(tally.requests, reqs.size());
        EXPECT_EQ(tally.responses_validated, reqs.size());
        EXPECT_EQ(tally.conforming, reqs.size());
        EXPECT_EQ(tally.violating, 0u);
    }
} // namespace
