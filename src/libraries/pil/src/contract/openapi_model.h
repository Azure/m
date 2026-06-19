// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

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
        nlohmann::json body_schema;

        // Header names the response declares as required.
        std::vector<std::string> required_headers;
    };

    //
    // A single operation: one HTTP method on one path template.
    //
    struct openapi_operation
    {
        std::string method;        // Upper-case: "GET", "POST", ...
        std::string path_template; // e.g. "/items/{id}"

        // Pre-split path template segments (no empty segments). A segment of the
        // form "{name}" is a capture; all others are literals.
        std::vector<std::string> segments;

        std::vector<openapi_parameter> parameters;

        // Request body. body schema is null json when the operation has no body.
        bool           has_request_body{false};
        nlohmann::json request_body_schema;
        nlohmann::json request_body_example; // null json when none

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
    // Load a spec (YAML or JSON bytes) into a normalized model. The caller owns
    // file I/O and passes the document text. Throws std::runtime_error when the
    // text is not valid YAML/JSON, is not an object, lacks a recognizable
    // version, or has a malformed `paths` structure (diagnostic-by-exception,
    // mirroring parse_pilcfg).
    //
    openapi_model
    load_openapi_model(std::string_view spec_text);

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
