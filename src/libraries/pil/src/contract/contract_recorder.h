// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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
}
