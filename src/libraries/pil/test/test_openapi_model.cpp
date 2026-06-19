// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Unit tests for the internal OpenAPI ("Swagger") model loader + path matcher
// (M-HWC-CONTRACT-MODEL, D-HWC-8). Specs are supplied as inline YAML/JSON text;
// no file I/O. Covers version detection, operation/parameter/body/response
// extraction across OAS 2.0 and 3.x, path-template matching, and malformed-spec
// diagnostics.
//

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "contract/openapi_model.h"

using namespace std::string_view_literals;
using m::pil::load_openapi_model;
using m::pil::match_operation;
using m::pil::normalize_path_key;
using m::pil::openapi_version;
using m::pil::parameter_location;
using m::pil::ref_resolver;

namespace
{
    // A minimal but realistic OAS 3.0 document reused across several tests.
    constexpr std::string_view k_petstore_3_0 = R"(
openapi: "3.0.3"
info:
  title: Pet Store
  version: "1.0.0"
paths:
  /pets:
    get:
      parameters:
        - name: limit
          in: query
          required: false
          schema:
            type: integer
          example: 25
      responses:
        "200":
          description: A list of pets
          content:
            application/json:
              schema:
                type: array
        default:
          description: error
    post:
      requestBody:
        content:
          application/json:
            schema:
              type: object
            example:
              name: Fido
      responses:
        "201":
          description: created
          headers:
            Location:
              required: true
              schema:
                type: string
  /pets/{petId}:
    get:
      parameters:
        - name: petId
          in: path
          schema:
            type: string
      responses:
        "200":
          description: one pet
          content:
            application/json:
              schema:
                type: object
)";
} // namespace

//--------------------------------------------------------------------------
// Version detection
//--------------------------------------------------------------------------

TEST(OpenApiModel, DetectsVersion30)
{
    auto const model = load_openapi_model(R"(openapi: "3.0.0"
paths: {})"sv);
    EXPECT_EQ(model.version, openapi_version::v3_0);
}

TEST(OpenApiModel, DetectsVersion31)
{
    auto const model = load_openapi_model(R"(openapi: "3.1.0"
paths: {})"sv);
    EXPECT_EQ(model.version, openapi_version::v3_1);
}

TEST(OpenApiModel, DetectsSwagger20)
{
    auto const model = load_openapi_model(R"(swagger: "2.0"
paths: {})"sv);
    EXPECT_EQ(model.version, openapi_version::v2_0);
}

TEST(OpenApiModel, MissingVersionThrows)
{
    EXPECT_ANY_THROW(load_openapi_model(R"(paths: {})"sv));
}

TEST(OpenApiModel, UnsupportedSwaggerVersionThrows)
{
    EXPECT_ANY_THROW(load_openapi_model(R"(swagger: "1.2"
paths: {})"sv));
}

//--------------------------------------------------------------------------
// Malformed inputs
//--------------------------------------------------------------------------

TEST(OpenApiModel, NonObjectRootThrows)
{
    EXPECT_ANY_THROW(load_openapi_model("- a\n- b"sv)); // sequence root
    EXPECT_ANY_THROW(load_openapi_model("\"just a string\""sv));
}

TEST(OpenApiModel, PathsNotObjectThrows)
{
    EXPECT_ANY_THROW(load_openapi_model(R"(openapi: "3.0.0"
paths: []
)"sv));
}

TEST(OpenApiModel, NoPathsYieldsEmptyModel)
{
    auto const model = load_openapi_model(R"(openapi: "3.0.0")"sv);
    EXPECT_EQ(model.version, openapi_version::v3_0);
    EXPECT_TRUE(model.operations.empty());
}

//--------------------------------------------------------------------------
// Operation / parameter / body / response extraction
//--------------------------------------------------------------------------

TEST(OpenApiModel, ExtractsOperations)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    // GET /pets, POST /pets, GET /pets/{petId}
    EXPECT_EQ(model.operations.size(), 3u);
}

TEST(OpenApiModel, ExtractsQueryParameterAndExample)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->operation->parameters.size(), 1u);
    auto const& p = m->operation->parameters.front();
    EXPECT_EQ(p.name, "limit");
    EXPECT_EQ(p.location, parameter_location::query);
    EXPECT_FALSE(p.required);
    EXPECT_EQ(p.schema.value("type", ""), "integer");
    EXPECT_EQ(p.example.get<int>(), 25);
}

TEST(OpenApiModel, PathParameterIsImplicitlyRequired)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets/42");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->operation->parameters.size(), 1u);
    auto const& p = m->operation->parameters.front();
    EXPECT_EQ(p.name, "petId");
    EXPECT_EQ(p.location, parameter_location::path);
    EXPECT_TRUE(p.required);
}

TEST(OpenApiModel, ExtractsOas3RequestBodySchemaAndExample)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "POST", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->has_request_body);
    EXPECT_EQ(m->operation->request_body_schema.value("type", ""), "object");
    EXPECT_EQ(m->operation->request_body_example.value("name", ""), "Fido");
}

TEST(OpenApiModel, ExtractsResponsesAndDefault)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->responses.contains(200));
    EXPECT_TRUE(m->operation->default_response.has_value());
    EXPECT_FALSE(m->operation->responses.contains(404));
}

TEST(OpenApiModel, ExtractsRequiredResponseHeaders)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "POST", "/pets");
    ASSERT_TRUE(m.has_value());
    auto const it = m->operation->responses.find(201);
    ASSERT_NE(it, m->operation->responses.end());
    ASSERT_EQ(it->second.required_headers.size(), 1u);
    EXPECT_EQ(it->second.required_headers.front(), "Location");
}

TEST(OpenApiModel, Oas2BodyParameterBecomesRequestBody)
{
    constexpr std::string_view spec = R"(
swagger: "2.0"
paths:
  /widgets:
    post:
      parameters:
        - name: body
          in: body
          schema:
            type: object
      responses:
        "200":
          description: ok
          schema:
            type: object
)";
    auto const model = load_openapi_model(spec);
    EXPECT_EQ(model.version, openapi_version::v2_0);
    auto const m = match_operation(model, "POST", "/widgets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->has_request_body);
    EXPECT_EQ(m->operation->request_body_schema.value("type", ""), "object");
    // The body parameter is not surfaced as a normal parameter.
    EXPECT_TRUE(m->operation->parameters.empty());
    // OAS2 response schema is captured.
    auto const it = m->operation->responses.find(200);
    ASSERT_NE(it, m->operation->responses.end());
    EXPECT_EQ(it->second.body_schema.value("type", ""), "object");
}

//--------------------------------------------------------------------------
// Path-template matching
//--------------------------------------------------------------------------

TEST(OpenApiModel, MatchLiteralPath)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->operation->path_template, "/pets");
    EXPECT_TRUE(m->path_parameters.empty());
}

TEST(OpenApiModel, MatchSingleParamCapture)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets/99");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->path_parameters.size(), 1u);
    EXPECT_EQ(m->path_parameters.front().first, "petId");
    EXPECT_EQ(m->path_parameters.front().second, "99");
}

TEST(OpenApiModel, MatchIsMethodCaseInsensitive)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    EXPECT_TRUE(match_operation(model, "get", "/pets").has_value());
    EXPECT_TRUE(match_operation(model, "Get", "/pets").has_value());
}

TEST(OpenApiModel, MatchStripsQueryString)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets?limit=10");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->operation->path_template, "/pets");
}

TEST(OpenApiModel, NoMatchOnSegmentCountOrMethod)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    EXPECT_FALSE(match_operation(model, "GET", "/pets/1/owners").has_value());
    EXPECT_FALSE(match_operation(model, "DELETE", "/pets").has_value());
    EXPECT_FALSE(match_operation(model, "GET", "/unknown").has_value());
}

TEST(OpenApiModel, MostSpecificLiteralBeatsTemplate)
{
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /pets/{petId}:
    get:
      responses:
        "200":
          description: by id
  /pets/featured:
    get:
      responses:
        "200":
          description: featured
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "GET", "/pets/featured");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->operation->path_template, "/pets/featured");
    EXPECT_TRUE(m->path_parameters.empty());
}

//--------------------------------------------------------------------------
// JSON input (JSON is a subset of YAML, so the same loader handles it)
//--------------------------------------------------------------------------

TEST(OpenApiModel, LoadsJsonSpec)
{
    constexpr std::string_view spec = R"({
      "openapi": "3.0.0",
      "paths": {
        "/ping": {
          "get": { "responses": { "200": { "description": "pong" } } }
        }
      }
    })";
    auto const model = load_openapi_model(spec);
    EXPECT_EQ(model.version, openapi_version::v3_0);
    EXPECT_TRUE(match_operation(model, "GET", "/ping").has_value());
}

//--------------------------------------------------------------------------
// M-HWC-CONTRACT-REFS: $ref bundle resolution, media-typed bodies, query
// discriminators, validation eligibility (D-HWC-9).
//--------------------------------------------------------------------------

namespace
{
    // Build a ref_resolver backed by an in-memory map of bundle-relative path
    // -> document bytes. Mirrors how a sibling-directory reader would behave
    // under `.pilcfg`, but with no file I/O.
    ref_resolver
    map_resolver(std::map<std::string, std::string> docs)
    {
        return [docs = std::move(docs)](std::string_view rel)
            -> std::optional<std::string>
        {
            auto const it = docs.find(std::string(rel));
            if (it == docs.end())
                return std::nullopt;
            return it->second;
        };
    }
} // namespace

TEST(OpenApiModel, NormalizePathKeySplitsQueryDiscriminator)
{
    auto const parts = normalize_path_key("/machine?comp=package"sv);
    EXPECT_EQ(parts.clean_path, "/machine");
    ASSERT_EQ(parts.discriminators.size(), 1u);
    EXPECT_EQ(parts.discriminators[0].first, "comp");
    EXPECT_EQ(parts.discriminators[0].second, "package");
}

TEST(OpenApiModel, NormalizePathKeyNoQueryIsUnchanged)
{
    auto const parts = normalize_path_key("/machine/{id}"sv);
    EXPECT_EQ(parts.clean_path, "/machine/{id}");
    EXPECT_TRUE(parts.discriminators.empty());
}

TEST(OpenApiModel, ResolvesInternalRef)
{
    // A $ref into #/components/schemas within the same document.
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /widgets:
    post:
      requestBody:
        content:
          application/json:
            schema:
              $ref: "#/components/schemas/Widget"
      responses:
        "201":
          description: created
components:
  schemas:
    Widget:
      type: object
      properties:
        kind:
          type: string
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "POST", "/widgets");
    ASSERT_TRUE(m.has_value());
    auto const& body = m->operation->request_body_schema;
    ASSERT_TRUE(body.contains("type"));
    EXPECT_EQ(body["type"], "object");
    EXPECT_TRUE(body["properties"].contains("kind"));
}

TEST(OpenApiModel, ResolvesRelativeFileRef)
{
    constexpr std::string_view root = R"(
openapi: "3.0.0"
paths:
  /widgets:
    post:
      requestBody:
        content:
          application/json:
            schema:
              $ref: "schemas.yml#/Widget"
      responses:
        "201":
          description: created
)";
    constexpr std::string_view schemas = R"(
Widget:
  type: object
  properties:
    color:
      type: string
)";
    auto const model = load_openapi_model(
        root, map_resolver({{"schemas.yml", std::string(schemas)}}));
    auto const m = match_operation(model, "POST", "/widgets");
    ASSERT_TRUE(m.has_value());
    auto const& body = m->operation->request_body_schema;
    EXPECT_EQ(body["type"], "object");
    EXPECT_TRUE(body["properties"].contains("color"));
}

TEST(OpenApiModel, ResolvesTransitiveRefAcrossDocuments)
{
    // root -> schemas.yml -> common.yml
    constexpr std::string_view root = R"(
openapi: "3.0.0"
paths:
  /widgets:
    get:
      responses:
        "200":
          description: ok
          content:
            application/json:
              schema:
                $ref: "schemas.yml#/Widget"
)";
    constexpr std::string_view schemas = R"(
Widget:
  type: object
  properties:
    id:
      $ref: "common.yml#/Id"
)";
    constexpr std::string_view common = R"(
Id:
  type: string
  format: uuid
)";
    auto const model = load_openapi_model(
        root,
        map_resolver({{"schemas.yml", std::string(schemas)},
                      {"common.yml", std::string(common)}}));
    auto const m = match_operation(model, "GET", "/widgets");
    ASSERT_TRUE(m.has_value());
    auto const& resp   = m->operation->responses.at(200);
    auto const& schema = resp.body_schema;
    ASSERT_TRUE(schema["properties"].contains("id"));
    EXPECT_EQ(schema["properties"]["id"]["format"], "uuid");
}

TEST(OpenApiModel, UnresolvedRefThrows)
{
    constexpr std::string_view root = R"(
openapi: "3.0.0"
paths:
  /widgets:
    post:
      requestBody:
        content:
          application/json:
            schema:
              $ref: "missing.yml#/Widget"
      responses:
        "201":
          description: created
)";
    // No resolver supplied: the external document cannot be fetched.
    EXPECT_ANY_THROW(load_openapi_model(root));
    // Resolver that returns nullopt for the requested document.
    EXPECT_ANY_THROW(load_openapi_model(
        root, map_resolver({{"other.yml", "{}"}})));
}

TEST(OpenApiModel, RefCycleIsBroken)
{
    // A self-referential schema must not loop forever.
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /nodes:
    get:
      responses:
        "200":
          description: ok
          content:
            application/json:
              schema:
                $ref: "#/components/schemas/Node"
components:
  schemas:
    Node:
      type: object
      properties:
        next:
          $ref: "#/components/schemas/Node"
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "GET", "/nodes");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->operation->responses.at(200).body_schema["type"], "object");
}

TEST(OpenApiModel, CapturesMultipleMediaTypeBodies)
{
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /docs:
    post:
      requestBody:
        content:
          application/json:
            schema:
              type: object
          text/xml:
            schema:
              type: string
      responses:
        "200":
          description: ok
          content:
            application/json:
              schema:
                type: object
            text/xml:
              schema:
                type: string
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "POST", "/docs");
    ASSERT_TRUE(m.has_value());
    auto const& req = m->operation->request_body_content;
    EXPECT_EQ(req.size(), 2u);
    ASSERT_TRUE(req.count("application/json"));
    ASSERT_TRUE(req.count("text/xml"));
    EXPECT_EQ(req.at("application/json").schema["type"], "object");
    EXPECT_EQ(req.at("text/xml").schema["type"], "string");

    auto const& resp = m->operation->responses.at(200).content;
    EXPECT_EQ(resp.size(), 2u);
    ASSERT_TRUE(resp.count("text/xml"));
    EXPECT_EQ(resp.at("text/xml").schema["type"], "string");
}

TEST(OpenApiModel, Oas2BodyMapsUnderConsumesMediaType)
{
    constexpr std::string_view spec = R"(
swagger: "2.0"
consumes:
  - text/xml
paths:
  /docs:
    post:
      parameters:
        - name: body
          in: body
          schema:
            type: string
      responses:
        "200":
          description: ok
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "POST", "/docs");
    ASSERT_TRUE(m.has_value());
    auto const& req = m->operation->request_body_content;
    ASSERT_TRUE(req.count("text/xml"));
    EXPECT_EQ(req.at("text/xml").schema["type"], "string");
}

TEST(OpenApiModel, QueryDiscriminatorRoutesToCorrectOperation)
{
    // Two operations on the same clean path discriminated by a query value.
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  "/machine?comp=package":
    get:
      responses:
        "200":
          description: package
  "/machine?comp=packageStatus":
    get:
      responses:
        "200":
          description: status
)";
    auto const model = load_openapi_model(spec);

    auto const a = match_operation(model, "GET", "/machine?comp=package");
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->operation->query_discriminators.size(), 1u);
    EXPECT_EQ(a->operation->query_discriminators[0].second, "package");

    auto const b = match_operation(model, "GET", "/machine?comp=packageStatus");
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(b->operation->query_discriminators.size(), 1u);
    EXPECT_EQ(b->operation->query_discriminators[0].second, "packageStatus");

    // No matching discriminator value -> no match.
    EXPECT_FALSE(match_operation(model, "GET", "/machine?comp=other").has_value());
}

TEST(OpenApiModel, DiscriminatorBecomesRequiredQueryParameter)
{
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  "/machine?comp=package":
    get:
      responses:
        "200":
          description: ok
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "GET", "/machine?comp=package");
    ASSERT_TRUE(m.has_value());
    bool found = false;
    for (auto const& p : m->operation->parameters)
    {
        if (p.name == "comp")
        {
            found = true;
            EXPECT_EQ(p.location, parameter_location::query);
            EXPECT_TRUE(p.required);
        }
    }
    EXPECT_TRUE(found);
}

TEST(OpenApiModel, ValidationEligibilityDefaultsTrue)
{
    auto const model = load_openapi_model(k_petstore_3_0);
    auto const m     = match_operation(model, "GET", "/pets");
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->operation->validation_eligible);
}

TEST(OpenApiModel, ValidationEligibilityHonorsExtension)
{
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /opaque:
    get:
      x-validated: false
      responses:
        "200":
          description: ok
  /checked:
    get:
      responses:
        "200":
          description: ok
)";
    auto const model = load_openapi_model(spec);
    auto const opaque = match_operation(model, "GET", "/opaque");
    ASSERT_TRUE(opaque.has_value());
    EXPECT_FALSE(opaque->operation->validation_eligible);

    auto const checked = match_operation(model, "GET", "/checked");
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(checked->operation->validation_eligible);
}

TEST(OpenApiModel, ValidationEligibilityInheritsFromPathItem)
{
    // x-validated on the path item is inherited by its operations.
    constexpr std::string_view spec = R"(
openapi: "3.0.0"
paths:
  /opaque:
    x-validated: false
    get:
      responses:
        "200":
          description: ok
)";
    auto const model = load_openapi_model(spec);
    auto const m     = match_operation(model, "GET", "/opaque");
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->operation->validation_eligible);
}

