// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "contract/openapi_model.h"

//
// HTTP contract recorder: derive an OpenAPI contract from observed traffic
// (the "derive the contracts" path of the wire-capture lifecycle demo).
//
// This header is internal to m_pil (lives under src/, not include/). The public
// façade arrives in REC-4.
//
// REC-2 piece: body-shape inference. Given one or more observed JSON bodies for
// the same position (a request body, or a response body for one status), infer a
// minimal JSON Schema describing their common shape. The schema is deliberately
// permissive — it constrains structure (object field names, value types, array
// element shape) but not values — so that conforming traffic validates and only
// genuinely divergent traffic is rejected.
//

namespace m::pil
{
    //
    // Infer a minimal JSON Schema for a single observed JSON value.
    //
    //   object  -> { "type":"object", "properties":{...}, "required":[all keys] }
    //   array   -> { "type":"array", "items": <merged element schema> }
    //   string  -> { "type":"string" }
    //   integer -> { "type":"integer" }   (number_integer / number_unsigned)
    //   number  -> { "type":"number" }    (floating point)
    //   boolean -> { "type":"boolean" }
    //   null    -> { "type":"null" }
    //
    nlohmann::json
    infer_json_schema(nlohmann::json const& sample);

    //
    // Infer a schema covering every sample. Object property sets are unioned;
    // `required` is the intersection of keys present in every object sample (a
    // field seen in only some samples is optional). Integer and number widen to
    // number; otherwise differing types collapse to the empty (accept-any)
    // schema `{}`. An empty sample set yields `{}` (accept any).
    //
    nlohmann::json
    infer_json_schema(std::vector<nlohmann::json> const& samples);

    //
    // Merge two inferred schemas under the rules above. Exposed for testing the
    // union/intersection/widening behavior directly.
    //
    nlohmann::json
    merge_json_schema(nlohmann::json const& a, nlohmann::json const& b);

    //
    // A header name/value pair, matching the public http_header shape
    // (std::pair<std::string,std::string>) so reassembled crossings feed the
    // recorder directly without an adapter.
    //
    using recorder_header = std::pair<std::string, std::string>;

    //
    // http_contract_recorder — accumulate observed request/response crossings
    // and derive an OpenAPI document (REC-3).
    //
    // Crossings are correlated by HTTP method + clean path (the query string is
    // stripped; the observed path is used verbatim as the path template — a
    // deliberate v1 simplification, since the demo endpoints are fixed paths).
    // Re-observing the same crossing is idempotent in shape: JSON bodies are
    // accumulated as inference samples (REC-2), so `required` fields narrow to
    // the intersection seen across observations. Declared response headers are
    // the intersection of headers present on every response of that status,
    // minus volatile transport headers (Date, Connection, Content-Length, …).
    //
    class http_contract_recorder
    {
    public:
        http_contract_recorder() = default;

        // Record a request crossing. `body` is the raw (already-decoded) body
        // bytes; it is parsed as JSON only when the Content-Type is JSON (or
        // absent and the bytes parse as JSON).
        void
        observe_request(std::string_view                    method,
                        std::string_view                    path,
                        std::vector<recorder_header> const& headers,
                        std::string_view                    body);

        // Record a response crossing for the given request method + path.
        void
        observe_response(std::string_view                    method,
                         std::string_view                    path,
                         std::uint16_t                       status,
                         std::vector<recorder_header> const& headers,
                         std::string_view                    body);

        // Build the derived, normalized OpenAPI model from what was observed.
        openapi_model
        build_model() const;

        // Serialize the derived model to OpenAPI YAML (build_model + emit).
        std::string
        emit_spec() const;

        // Number of distinct (method, path) operations observed so far.
        std::size_t
        operation_count() const noexcept
        {
            return m_operations.size();
        }

    private:
        struct response_acc
        {
            std::vector<nlohmann::json>          json_bodies;
            std::set<std::string>                media_types;
            std::optional<std::set<std::string>> required_headers; // intersection
        };

        struct operation_acc
        {
            bool                                 has_request_body{false};
            std::vector<nlohmann::json>          request_json_bodies;
            std::set<std::string>                request_media_types;
            std::map<std::uint16_t, response_acc> responses;
        };

        // Keyed by (UPPERCASE method, clean path).
        std::map<std::pair<std::string, std::string>, operation_acc> m_operations;
    };
}
