// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Tests for the live HTTP contract provider, the validating facet, and the
// validate-mode integration over the synthetic edge (M-HWC-CONTRACT-VALIDATE,
// D-HWC-8, D-HWC-9, D6). Specs are supplied as inline YAML; no file I/O.
//

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "contract/contract_errors.h"
#include "contract/contract_validating_facet.h"
#include "contract/http_contract_provider.h"

#include <m/pil/http_contract_interfaces.h>

namespace
{
    using m::pil::contract_error;
    using m::pil::contract_validating_facet;
    using m::pil::http_header;
    using m::pil::ihttp_contract;
    using m::pil::ihttp_contract_document;
    using m::pil::make_http_contract_provider;

    using request_code  = ihttp_contract_document::validate_request_result_code;
    using response_code = ihttp_contract_document::validate_response_result_code;

    //
    // A tiny OAS 3.0 spec: a GET with a required query param and a JSON 200
    // response (required header + body schema), and a POST with a required JSON
    // body, plus a not-eligible operation and a text/xml route.
    //
    constexpr std::string_view kSpec = R"(
openapi: 3.0.0
info:
  title: contract-test
  version: "1.0"
paths:
  /items/{id}:
    get:
      parameters:
        - name: id
          in: path
          required: true
          schema: { type: string }
        - name: verbose
          in: query
          required: true
          schema: { type: string }
        - name: X-Token
          in: header
          required: true
          schema: { type: string }
      responses:
        '200':
          description: ok
          headers:
            X-Trace:
              required: true
              schema: { type: string }
          content:
            application/json:
              schema:
                type: object
                required: [name]
                properties:
                  name: { type: string }
  /widgets:
    post:
      requestBody:
        required: true
        content:
          application/json:
            schema:
              type: object
              required: [size]
              properties:
                size: { type: integer }
      responses:
        '201':
          description: created
  /legacy:
    get:
      x-validated: false
      responses:
        '200':
          description: ok
  /report:
    get:
      responses:
        '200':
          description: ok
          content:
            text/xml:
              schema:
                type: object
                required: [name]
                properties:
                  name: { type: string }
)";

    std::vector<std::uint8_t>
    bytes(std::string_view s)
    {
        return {s.begin(), s.end()};
    }

    std::unique_ptr<ihttp_contract_document>
    load_test_document()
    {
        auto provider = make_http_contract_provider();
        auto doc      = provider->load(kSpec);
        EXPECT_NE(doc, nullptr);
        return doc;
    }

    //------------------------------------------------------------------------
    // VALIDATE-1: live provider / document
    //------------------------------------------------------------------------

    TEST(HttpContractProvider, LoadYieldsDocument)
    {
        auto provider = make_http_contract_provider();
        std::unique_ptr<ihttp_contract_document> doc;
        std::error_code                          ec;
        auto const                               d = provider->load(ihttp_contract::load_flags{}, kSpec, doc, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
        EXPECT_NE(doc, nullptr);
    }

    TEST(HttpContractProvider, MalformedSpecSurfacesThroughEc)
    {
        auto provider = make_http_contract_provider();
        std::unique_ptr<ihttp_contract_document> doc;
        std::error_code                          ec;
        provider->load(ihttp_contract::load_flags{}, "this: is: not: openapi: ::", doc, ec);
        EXPECT_TRUE(ec);
        EXPECT_EQ(doc, nullptr);
    }

    TEST(HttpContractDocument, ConformingRequestYieldsNoViolation)
    {
        auto                          doc = load_test_document();
        std::vector<http_header>      headers{{"X-Token", "abc"}};
        std::error_code               ec;
        auto const d = doc->validate_request("GET", "/items/42?verbose=1", headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    TEST(HttpContractDocument, UnknownOperationDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers;
        std::error_code          ec;
        auto const               d = doc->validate_request("GET", "/nope", headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), request_code::unknown_operation);
    }

    TEST(HttpContractDocument, MissingRequiredQueryParameterDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"X-Token", "abc"}};
        std::error_code          ec;
        auto const               d = doc->validate_request("GET", "/items/42", headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), request_code::parameter_invalid);
    }

    TEST(HttpContractDocument, MissingRequiredHeaderParameterDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers; // no X-Token
        std::error_code          ec;
        auto const d = doc->validate_request("GET", "/items/42?verbose=1", headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), request_code::parameter_invalid);
    }

    TEST(HttpContractDocument, ConformingRequestBodyPasses)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"Content-Type", "application/json"}};
        auto const               body = bytes(R"({"size": 7})");
        std::error_code          ec;
        auto const               d = doc->validate_request("POST", "/widgets", headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    TEST(HttpContractDocument, RequestBodySchemaInvalidDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"Content-Type", "application/json"}};
        auto const               body = bytes(R"({"size": "not-an-int"})");
        std::error_code          ec;
        auto const               d = doc->validate_request("POST", "/widgets", headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), request_code::body_schema_invalid);
    }

    TEST(HttpContractDocument, MissingRequiredRequestBodyFieldDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"Content-Type", "application/json"}};
        auto const               body = bytes(R"({"other": 1})");
        std::error_code          ec;
        auto const               d = doc->validate_request("POST", "/widgets", headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), request_code::body_schema_invalid);
    }

    TEST(HttpContractDocument, ConformingResponsePasses)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"X-Trace", "t1"}, {"Content-Type", "application/json"}};
        auto const               body = bytes(R"({"name": "widget"})");
        std::error_code          ec;
        auto const d = doc->validate_response("GET", "/items/42?verbose=1", 200, headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    TEST(HttpContractDocument, ResponseUndeclaredStatusDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"X-Trace", "t1"}};
        std::error_code          ec;
        auto const d = doc->validate_response("GET", "/items/42?verbose=1", 500, headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), response_code::undeclared_status);
    }

    TEST(HttpContractDocument, ResponseBodySchemaInvalidDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"X-Trace", "t1"}, {"Content-Type", "application/json"}};
        auto const               body = bytes(R"({"name": 123})");
        std::error_code          ec;
        auto const d = doc->validate_response("GET", "/items/42?verbose=1", 200, headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), response_code::body_schema_invalid);
    }

    TEST(HttpContractDocument, ResponseMissingRequiredHeaderDetected)
    {
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"Content-Type", "application/json"}}; // no X-Trace
        auto const               body = bytes(R"({"name": "widget"})");
        std::error_code          ec;
        auto const d = doc->validate_response("GET", "/items/42?verbose=1", 200, headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), response_code::missing_header);
    }

    TEST(HttpContractDocument, NonJsonBodySkipsBodyValidation)
    {
        // /report declares only a text/xml body schema; an XML body that is not
        // valid JSON must NOT be flagged (media-type awareness, D-HWC-9).
        auto                     doc = load_test_document();
        std::vector<http_header> headers{{"Content-Type", "text/xml"}};
        auto const               body = bytes("<report><name>x</name></report>");
        std::error_code          ec;
        auto const               d = doc->validate_response("GET", "/report", 200, headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    TEST(HttpContractDocument, ValidationIneligibleOperationSkipped)
    {
        // /legacy is x-validated:false; even an "off-contract" response is skipped.
        auto                     doc = load_test_document();
        std::vector<http_header> headers;
        std::error_code          ec;
        auto const               d = doc->validate_response("GET", "/legacy", 503, headers, {}, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    //------------------------------------------------------------------------
    // VALIDATE-2: validating facet
    //------------------------------------------------------------------------

    TEST(ContractValidatingFacet, NullDocumentYieldsNoError)
    {
        contract_validating_facet facet{nullptr,
                                        contract_validating_facet::flags::surface_violations};
        std::vector<http_header>  headers;
        EXPECT_FALSE(facet.on_request("GET", "/anything", headers, {}));
    }

    TEST(ContractValidatingFacet, ConformingYieldsNoError)
    {
        std::shared_ptr<ihttp_contract_document> doc = load_test_document();
        contract_validating_facet                facet{doc,
                                        contract_validating_facet::flags::surface_violations};
        std::vector<http_header>                 headers{{"X-Token", "abc"}};
        EXPECT_FALSE(facet.on_request("GET", "/items/42?verbose=1", headers, {}));
    }

    TEST(ContractValidatingFacet, TracesButDoesNotSurfaceByDefault)
    {
        std::shared_ptr<ihttp_contract_document> doc = load_test_document();
        contract_validating_facet                facet{doc}; // surfacing off
        std::vector<http_header>                 headers;
        // Missing required query param + header -> violation, but off by default.
        EXPECT_FALSE(facet.on_request("GET", "/items/42", headers, {}));
    }

    TEST(ContractValidatingFacet, SurfacesRequestViolationWhenOptedIn)
    {
        std::shared_ptr<ihttp_contract_document> doc = load_test_document();
        contract_validating_facet                facet{doc,
                                        contract_validating_facet::flags::surface_violations};
        std::vector<http_header>                 headers;
        auto const                               ec = facet.on_request("GET", "/nope", headers, {});
        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, make_error_code(contract_error::request_violation));
    }

    TEST(ContractValidatingFacet, SurfacesResponseViolationWhenOptedIn)
    {
        std::shared_ptr<ihttp_contract_document> doc = load_test_document();
        contract_validating_facet                facet{doc,
                                        contract_validating_facet::flags::surface_violations};
        std::vector<http_header>                 headers{{"X-Trace", "t1"}};
        auto const ec = facet.on_response("GET", "/items/42?verbose=1", 500, headers, {});
        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, make_error_code(contract_error::response_violation));
    }

    //------------------------------------------------------------------------
    // VALIDATE-3: validate mode over the synthetic edge
    //------------------------------------------------------------------------

    TEST(ContractValidateIntegration, ConformingAndViolatingTrafficAcrossEdge)
    {
        std::shared_ptr<ihttp_contract_document> doc = load_test_document();
        contract_validating_facet                facet{doc,
                                        contract_validating_facet::flags::surface_violations};

        // A conforming request + its conforming response cross the edge cleanly.
        std::vector<http_header> good_req_headers{{"X-Token", "abc"}};
        EXPECT_FALSE(facet.on_request("GET", "/items/7?verbose=yes", good_req_headers, {}));

        std::vector<http_header> good_resp_headers{{"X-Trace", "tr"},
                                                   {"Content-Type", "application/json"}};
        auto const               good_body = bytes(R"({"name": "seven"})");
        EXPECT_FALSE(
            facet.on_response("GET", "/items/7?verbose=yes", 200, good_resp_headers, good_body));

        // A violating request (missing the required header) is detected.
        std::vector<http_header> bad_req_headers; // no X-Token
        EXPECT_EQ(facet.on_request("GET", "/items/7?verbose=yes", bad_req_headers, {}),
                  make_error_code(contract_error::request_violation));

        // A violating response (body fails the schema) is detected.
        std::vector<http_header> bad_resp_headers{{"X-Trace", "tr"},
                                                  {"Content-Type", "application/json"}};
        auto const               bad_body = bytes(R"({"name": 999})");
        EXPECT_EQ(
            facet.on_response("GET", "/items/7?verbose=yes", 200, bad_resp_headers, bad_body),
            make_error_code(contract_error::response_violation));
    }
} // namespace
