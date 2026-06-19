// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Unit tests for the HTTP contract recorder's body-shape inference (REC-2):
// inferring a minimal JSON Schema from one or more observed JSON bodies.
//

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "contract/contract_recorder.h"

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
