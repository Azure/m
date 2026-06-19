// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

//
// Internal OpenAPI ("Swagger") model for the HWC HTTP contract surface (D-HWC-8).
//
// This is a flat, normalized representation of an OpenAPI document: a list of
// operations, each carrying its method, path template, parameters, optional
// request-body schema, and a status->response map. Body and parameter schemas
// are stored as nlohmann::json ready to feed a JSON Schema validator
// (M-HWC-CONTRACT-VALIDATE); examples are retained for the example-driven
// traffic generator (M-HWC-CONTRACT-DRIVE).
//
// The loader accepts the spec as text bytes (YAML or JSON — JSON is a subset of
// YAML, so a single YAML parse handles both) and never performs file I/O; the
// caller owns reading the file, mirroring parse_pilcfg's pure-text contract.
//
// This header is internal to m_pil (lives under src/, not include/). It is not
// part of the public PIL surface; the public façade arrives in
// M-HWC-CONTRACT-IFACE.
//

namespace m::pil
{
    //
    // The OpenAPI specification version family a document was authored in.
    // Internal model only; these values are not a wire contract.
    //
    enum class openapi_version
    {
        v2_0, // Swagger 2.0 (`swagger: "2.0"`)
        v3_0, // OpenAPI 3.0.x
        v3_1, // OpenAPI 3.1.x (JSON Schema 2020-12)
    };

    //
    // Where a (non-body) parameter is carried.
    //
    enum class parameter_location
    {
        path,
        query,
        header,
        cookie,
    };

    //
    // One media type's body description: its schema and optional example. Both
    // are stored as nlohmann::json (null when the spec gave none).
    //
    struct openapi_media_body
    {
        nlohmann::json schema;
        nlohmann::json example;
    };

    //
    // A single non-body operation parameter.
    //
    struct openapi_parameter
    {
        std::string        name;
        parameter_location location{parameter_location::query};
        bool               required{false};

        // JSON Schema for the parameter value (null json when the spec gave none).
        nlohmann::json schema;

        // Example value for drive mode (null json when none was supplied).
        nlohmann::json example;
    };

    //
    // A single declared response (keyed by status, or the default response).
    //
    struct openapi_response
    {
        // JSON Schema for the JSON response body (null json when none / non-JSON).
        // Convenience view of the application/json entry in `content`.
        nlohmann::json body_schema;

        // Every declared media type -> its schema + example (D-HWC-9). Includes
        // non-JSON content (e.g. text/xml). `body_schema` mirrors the JSON entry.
        std::map<std::string, openapi_media_body> content;

        // Header names the response declares as required.
        std::vector<std::string> required_headers;
    };

    //
    // A single operation: one HTTP method on one path template.
    //
    struct openapi_operation
    {
        std::string method;        // Upper-case: "GET", "POST", ...
        std::string path_template; // Clean path (query discriminator stripped), e.g. "/items/{id}"

        // Pre-split path template segments (no empty segments). A segment of the
        // form "{name}" is a capture; all others are literals.
        std::vector<std::string> segments;

        // Required query key=value pairs lifted from a query-in-path-key (e.g. a
        // path key of "/machine?comp=package" yields {"comp","package"}). The
        // matcher requires every pair to be present in the request query, and an
        // operation with more discriminators is the more specific match. Each
        // pair is also surfaced as a required query parameter in `parameters`.
        std::vector<std::pair<std::string, std::string>> query_discriminators;

        // Authored validation eligibility (vendor extension `x-validated`).
        // Defaults true; an operation (or its path item) declaring
        // `x-validated: false` is skipped by validate mode (D-HWC-9).
        bool validation_eligible{true};

        std::vector<openapi_parameter> parameters;

        // Request body. body schema is null json when the operation has no body.
        // `request_body_schema`/`request_body_example` are the application/json
        // convenience view; `request_body_content` is every declared media type.
        bool                                      has_request_body{false};
        nlohmann::json                            request_body_schema;
        nlohmann::json                            request_body_example; // null json when none
        std::map<std::string, openapi_media_body> request_body_content;

        // status code -> response (literal HTTP status, e.g. 200).
        std::map<int, openapi_response> responses;

        // The "default" response, when declared.
        std::optional<openapi_response> default_response;
    };

    //
    // A loaded, normalized OpenAPI document.
    //
    struct openapi_model
    {
        openapi_version                version{openapi_version::v3_0};
        std::vector<openapi_operation> operations;
    };

    //
    // Resolves a referenced document, given the bundle-relative path taken from a
    // `$ref` (e.g. "components/schemas.yml"), to that document's text bytes.
    // Returns nullopt when the path cannot be resolved. PIL decides what to fetch
    // and how to splice it; the caller decides where the bytes come from (an
    // in-memory map in tests, a sibling-directory read under `.pilcfg`). This
    // keeps file I/O at the boundary, mirroring parse_pilcfg (D-HWC-9).
    //
    using ref_resolver = std::function<std::optional<std::string>(std::string_view relative_path)>;

    //
    // The clean path plus the query discriminators lifted from an OpenAPI path
    // key. A key of "/machine?comp=package" normalizes to clean_path
    // "/machine" with discriminator {"comp","package"}.
    //
    struct path_key_parts
    {
        std::string                                      clean_path;
        std::vector<std::pair<std::string, std::string>> discriminators;
    };

    //
    // Split an OpenAPI path key into its clean path and any query discriminators
    // embedded in the key. Non-standard `?key=value` suffixes on a path key are
    // a routing discriminator; this helper lifts them out so they can also be
    // surfaced as required query parameters (D-HWC-9). A key with no query
    // returns the key unchanged and no discriminators.
    //
    path_key_parts
    normalize_path_key(std::string_view path_key);

    //
    // Load a spec (YAML or JSON bytes) into a normalized model, resolving `$ref`
    // bundles. The caller owns file I/O: the root document text is passed
    // directly, and any referenced documents are fetched through `resolver`
    // (relative-file and transitive refs). Internal refs (`#/...`) resolve within
    // the documents already loaded; an external ref encountered with no resolver
    // (or one returning nullopt) is a load diagnostic. Throws std::runtime_error
    // when the text is not valid YAML/JSON, is not an object, lacks a
    // recognizable version, has a malformed `paths` structure, or references a
    // document that cannot be resolved (diagnostic-by-exception, mirroring
    // parse_pilcfg).
    //
    openapi_model
    load_openapi_model(std::string_view spec_text, ref_resolver resolver = {});

    //
    // The result of matching a concrete request against the model: the matched
    // operation plus the captured path parameters (in template order).
    //
    struct openapi_path_match
    {
        openapi_operation const*                         operation{nullptr};
        std::vector<std::pair<std::string, std::string>> path_parameters;
    };

    //
    // Find the operation whose method + path template matches the given concrete
    // method and path. The method is matched case-insensitively; literal path
    // segments must match exactly; "{param}" segments capture one path segment.
    // Any query string / fragment on `path` is ignored. When several templates
    // match, the one with the fewest captures (the most specific) wins. Returns
    // nullopt when nothing matches. The returned pointer is valid for the
    // lifetime of `model`.
    //
    std::optional<openapi_path_match>
    match_operation(openapi_model const& model,
                    std::string_view     method,
                    std::string_view     path);
}
