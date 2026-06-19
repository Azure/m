// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Unit tests for the HTTP contract recorder's body-shape inference (REC-2):
// inferring a minimal JSON Schema from one or more observed JSON bodies.
//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <m/pil/http_contract_recorder.h>

#include "contract/contract_recorder.h"
#include "contract/http_contract_provider.h"
#include "contract/openapi_model.h"

using m::pil::infer_json_schema;
using m::pil::merge_json_schema;
using nlohmann::json;

namespace
{
    // Sorted vector of the schema's `required` field, for order-independent
    // comparison.
    std::vector<std::string>
    required_of(json const& schema)
    {
        std::vector<std::string> out;
        if (schema.contains("required"))
            for (auto const& k : schema["required"])
                out.push_back(k.get<std::string>());
        return out;
    }
} // namespace

//--------------------------------------------------------------------------
// Single-sample inference of each value kind
//--------------------------------------------------------------------------

TEST(ContractRecorderInfer, ScalarString)
{
    EXPECT_EQ(infer_json_schema(json("hi")), (json{{"type", "string"}}));
}

TEST(ContractRecorderInfer, ScalarInteger)
{
    EXPECT_EQ(infer_json_schema(json(42)), (json{{"type", "integer"}}));
    EXPECT_EQ(infer_json_schema(json(7u)), (json{{"type", "integer"}}));
}

TEST(ContractRecorderInfer, ScalarNumber)
{
    EXPECT_EQ(infer_json_schema(json(3.5)), (json{{"type", "number"}}));
}

TEST(ContractRecorderInfer, ScalarBooleanAndNull)
{
    EXPECT_EQ(infer_json_schema(json(true)), (json{{"type", "boolean"}}));
    EXPECT_EQ(infer_json_schema(json(nullptr)), (json{{"type", "null"}}));
}

TEST(ContractRecorderInfer, FlatObject)
{
    json const sample = {{"name", "Fido"}, {"age", 3}};
    json const schema = infer_json_schema(sample);

    EXPECT_EQ(schema["type"], "object");
    EXPECT_EQ(schema["properties"]["name"], (json{{"type", "string"}}));
    EXPECT_EQ(schema["properties"]["age"], (json{{"type", "integer"}}));
    EXPECT_EQ(required_of(schema), (std::vector<std::string>{"age", "name"}));
}

TEST(ContractRecorderInfer, NestedObject)
{
    json const sample = {{"pet", {{"name", "Fido"}}}};
    json const schema = infer_json_schema(sample);

    EXPECT_EQ(schema["properties"]["pet"]["type"], "object");
    EXPECT_EQ(schema["properties"]["pet"]["properties"]["name"], (json{{"type", "string"}}));
}

TEST(ContractRecorderInfer, ArrayOfScalars)
{
    json const schema = infer_json_schema(json::parse(R"([1, 2, 3])"));
    EXPECT_EQ(schema["type"], "array");
    EXPECT_EQ(schema["items"], (json{{"type", "integer"}}));
}

TEST(ContractRecorderInfer, EmptyArrayHasNoItems)
{
    json const schema = infer_json_schema(json::array());
    EXPECT_EQ(schema["type"], "array");
    EXPECT_FALSE(schema.contains("items"));
}

TEST(ContractRecorderInfer, ArrayOfObjects)
{
    json const schema =
        infer_json_schema(json::parse(R"([{"a":1},{"a":2}])"));
    EXPECT_EQ(schema["items"]["type"], "object");
    EXPECT_EQ(schema["items"]["properties"]["a"], (json{{"type", "integer"}}));
}

//--------------------------------------------------------------------------
// Multi-sample merge: union of properties, intersection of required
//--------------------------------------------------------------------------

TEST(ContractRecorderInfer, EmptySampleSetIsAcceptAny)
{
    EXPECT_EQ(infer_json_schema(std::vector<json>{}), json::object());
}

TEST(ContractRecorderInfer, RequiredIsIntersectionAcrossSamples)
{
    std::vector<json> const samples = {
        json{{"a", 1}, {"b", 2}},
        json{{"a", 9}, {"c", 3}},
    };
    json const schema = infer_json_schema(samples);

    // Properties are unioned.
    EXPECT_TRUE(schema["properties"].contains("a"));
    EXPECT_TRUE(schema["properties"].contains("b"));
    EXPECT_TRUE(schema["properties"].contains("c"));
    // Only "a" appears in every sample, so only "a" is required.
    EXPECT_EQ(required_of(schema), (std::vector<std::string>{"a"}));
}

TEST(ContractRecorderInfer, IntegerAndNumberWidenToNumber)
{
    std::vector<json> const samples = {json(1), json(2.5)};
    EXPECT_EQ(infer_json_schema(samples), (json{{"type", "number"}}));
}

TEST(ContractRecorderInfer, DifferingTypesCollapseToAcceptAny)
{
    std::vector<json> const samples = {json("x"), json(5)};
    EXPECT_EQ(infer_json_schema(samples), json::object());
}

TEST(ContractRecorderInfer, MergeRecursesIntoSharedObjectFields)
{
    json const a      = infer_json_schema(json{{"pet", {{"name", "Fido"}, {"tag", "x"}}}});
    json const b      = infer_json_schema(json{{"pet", {{"name", "Rex"}}}});
    json const merged = merge_json_schema(a, b);

    // "pet" is required in both, so required at the top level.
    EXPECT_EQ(required_of(merged), (std::vector<std::string>{"pet"}));
    // Within "pet", only "name" is in both samples.
    EXPECT_EQ(required_of(merged["properties"]["pet"]),
              (std::vector<std::string>{"name"}));
    EXPECT_TRUE(merged["properties"]["pet"]["properties"].contains("tag"));
}

TEST(ContractRecorderInfer, IdempotentReinference)
{
    json const sample = {{"name", "Fido"}, {"age", 3}};
    std::vector<json> const samples = {sample, sample, sample};
    EXPECT_EQ(infer_json_schema(samples), infer_json_schema(sample));
}

//--------------------------------------------------------------------------
// REC-3: http_contract_recorder accumulation + derived-spec emission
//--------------------------------------------------------------------------

using m::pil::http_contract_recorder;
using m::pil::load_openapi_model;
using m::pil::match_operation;
using m::pil::recorder_header;

namespace
{
    using headers = std::vector<recorder_header>;
    headers const k_json_ct = {{"Content-Type", "application/json"}};
} // namespace

TEST(ContractRecorder, RecordsSingleGetOperation)
{
    http_contract_recorder rec;
    rec.observe_request("GET", "/pets", {}, "");
    rec.observe_response("GET", "/pets", 200, k_json_ct, R"([{"name":"Fido"}])");

    EXPECT_EQ(rec.operation_count(), 1u);

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->responses.contains(200));
    EXPECT_FALSE(m->operation->has_request_body);
}

TEST(ContractRecorder, RecordsRequestBodySchema)
{
    http_contract_recorder rec;
    rec.observe_request("POST", "/pets", k_json_ct, R"({"name":"Fido","age":3})");
    rec.observe_response("POST", "/pets", 201, {{"Location", "/pets/1"}}, "");

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "POST", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->has_request_body);
    EXPECT_EQ(m->operation->request_body_schema["type"], "object");
    EXPECT_TRUE(m->operation->responses.contains(201));
}

TEST(ContractRecorder, StripsQueryFromPath)
{
    http_contract_recorder rec;
    rec.observe_response("GET", "/pets?limit=10", 200, k_json_ct, "[]");

    auto const model = rec.build_model();
    ASSERT_EQ(model.operations.size(), 1u);
    EXPECT_EQ(model.operations.front().path_template, "/pets");
}

TEST(ContractRecorder, AccumulatesMultipleStatuses)
{
    http_contract_recorder rec;
    rec.observe_response("GET", "/pets/1", 200, k_json_ct, R"({"name":"x"})");
    rec.observe_response("GET", "/pets/1", 404, k_json_ct, R"({"error":"nope"})");

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "GET", "/pets/1");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->responses.contains(200));
    EXPECT_TRUE(m->operation->responses.contains(404));
}

TEST(ContractRecorder, RequiredResponseHeadersAreIntersection)
{
    http_contract_recorder rec;
    // Both responses carry Content-Type; only the first carries X-Trace.
    rec.observe_response("GET", "/pets", 200,
                         {{"Content-Type", "application/json"}, {"X-Trace", "abc"}}, "[]");
    rec.observe_response("GET", "/pets", 200,
                         {{"Content-Type", "application/json"}}, "[]");

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    auto const& req = m->operation->responses.at(200).required_headers;
    // content-type present on every response -> required; x-trace not.
    EXPECT_NE(std::find(req.begin(), req.end(), "content-type"), req.end());
    EXPECT_EQ(std::find(req.begin(), req.end(), "x-trace"), req.end());
}

TEST(ContractRecorder, TransportHeadersAreNotRequired)
{
    http_contract_recorder rec;
    rec.observe_response("GET", "/pets", 200,
                         {{"Content-Type", "application/json"},
                          {"Date", "now"},
                          {"Content-Length", "2"},
                          {"Connection", "keep-alive"}},
                         "[]");

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    auto const& req = m->operation->responses.at(200).required_headers;
    EXPECT_EQ(std::find(req.begin(), req.end(), "date"), req.end());
    EXPECT_EQ(std::find(req.begin(), req.end(), "content-length"), req.end());
    EXPECT_EQ(std::find(req.begin(), req.end(), "connection"), req.end());
}

TEST(ContractRecorder, NonJsonBodyRecordsMediaTypeOnly)
{
    http_contract_recorder rec;
    rec.observe_response("GET", "/page", 200, {{"Content-Type", "text/html"}},
                         "<html></html>");

    auto const model = rec.build_model();
    auto const m     = match_operation(model, "GET", "/page");
    ASSERT_TRUE(m.has_value());
    auto const& content = m->operation->responses.at(200).content;
    EXPECT_TRUE(content.count("text/html"));
    EXPECT_FALSE(content.count("application/json"));
}

TEST(ContractRecorder, ObservationIsIdempotentInShape)
{
    http_contract_recorder rec;
    for (int i = 0; i < 3; ++i)
    {
        rec.observe_request("POST", "/pets", k_json_ct, R"({"name":"Fido","age":3})");
        rec.observe_response("POST", "/pets", 201, {{"Content-Type", "application/json"}},
                             R"({"id":1})");
    }
    EXPECT_EQ(rec.operation_count(), 1u);

    http_contract_recorder once;
    once.observe_request("POST", "/pets", k_json_ct, R"({"name":"Fido","age":3})");
    once.observe_response("POST", "/pets", 201, {{"Content-Type", "application/json"}},
                          R"({"id":1})");

    EXPECT_EQ(rec.emit_spec(), once.emit_spec());
}

TEST(ContractRecorder, EmittedSpecReloadsWithObservedOperations)
{
    http_contract_recorder rec;
    rec.observe_request("POST", "/pets", k_json_ct, R"({"name":"Fido"})");
    rec.observe_response("POST", "/pets", 201, {{"Content-Type", "application/json"}},
                         R"({"id":1})");
    rec.observe_response("GET", "/pets", 200, k_json_ct, R"([{"name":"Fido"}])");

    auto const reloaded = load_openapi_model(rec.emit_spec());
    EXPECT_TRUE(match_operation(reloaded, "POST", "/pets").has_value());
    EXPECT_TRUE(match_operation(reloaded, "GET", "/pets").has_value());
    auto const post = match_operation(reloaded, "POST", "/pets");
    ASSERT_TRUE(post.has_value());
    EXPECT_TRUE(post->operation->has_request_body);
    EXPECT_TRUE(post->operation->responses.contains(201));
}

//--------------------------------------------------------------------------
// REC-4: public façade (make_http_contract_recorder) + close-the-loop test
//--------------------------------------------------------------------------

using m::pil::http_header;
using m::pil::ihttp_contract_document;
using m::pil::make_http_contract_provider;
using m::pil::make_http_contract_recorder;

namespace
{
    std::vector<std::uint8_t>
    body_bytes(std::string_view s)
    {
        return {s.begin(), s.end()};
    }
} // namespace

// The façade exposes the same accumulation the internal recorder provides.
TEST(ContractRecorderFacade, FacadeAccumulatesAndEmits)
{
    auto                     rec = make_http_contract_recorder();
    std::vector<http_header> json_ct{{"Content-Type", "application/json"}};

    auto const req = body_bytes(R"({"name":"Fido","age":3})");
    rec->observe_request("POST", "/pets", json_ct, req);
    auto const resp = body_bytes(R"({"id":1})");
    rec->observe_response("POST", "/pets", 201, json_ct, resp);

    EXPECT_EQ(rec->operation_count(), 1u);

    auto const reloaded = m::pil::load_openapi_model(rec->emit_spec());
    EXPECT_TRUE(match_operation(reloaded, "POST", "/pets").has_value());
}

// The whole point of the recorder: a spec derived from clean traffic, when
// loaded back through the live provider, ACCEPTS conforming crossings and
// REJECTS divergent ones (the demo's derive -> validate round trip).
TEST(ContractRecorderFacade, ClosesTheLoopAcceptCleanRejectMutated)
{
    auto                     rec = make_http_contract_recorder();
    std::vector<http_header> json_ct{{"Content-Type", "application/json"}};

    // Derive: observe clean crossings only.
    rec->observe_request("POST", "/pets", json_ct, body_bytes(R"({"name":"Fido","age":3})"));
    rec->observe_response("POST", "/pets", 201, json_ct, body_bytes(R"({"id":1})"));

    // Load the derived spec through the live contract provider.
    auto                                     provider = make_http_contract_provider();
    std::unique_ptr<ihttp_contract_document> doc      = provider->load(rec->emit_spec());
    ASSERT_NE(doc, nullptr);

    // Clean crossings conform (false disposition, no error).
    {
        auto const            clean = body_bytes(R"({"name":"Rex","age":5})");
        std::error_code       ec;
        auto const            d = doc->validate_request("POST", "/pets", json_ct, clean, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }
    {
        auto const            clean = body_bytes(R"({"id":2})");
        std::error_code       ec;
        auto const            d = doc->validate_response("POST", "/pets", 201, json_ct, clean, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
    }

    // Mutated request: required "age" dropped and "name" wrong type.
    {
        auto const            bad = body_bytes(R"({"name":123})");
        std::error_code       ec;
        auto const            d = doc->validate_request("POST", "/pets", json_ct, bad, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(),
                  ihttp_contract_document::validate_request_result_code::body_schema_invalid);
    }

    // Mutated response: "id" wrong type.
    {
        auto const            bad = body_bytes(R"({"id":"two"})");
        std::error_code       ec;
        auto const            d = doc->validate_response("POST", "/pets", 201, json_ct, bad, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(),
                  ihttp_contract_document::validate_response_result_code::body_schema_invalid);
    }
}
