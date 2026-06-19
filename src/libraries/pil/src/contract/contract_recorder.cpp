// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "contract/contract_recorder.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace m::pil
{
    namespace
    {
        // The JSON Schema "type" keyword of a schema node, or "" when the schema
        // has none (the empty / accept-any schema).
        std::string
        schema_type(nlohmann::json const& schema)
        {
            if (schema.is_object())
            {
                if (auto const it = schema.find("type"); it != schema.end() && it->is_string())
                    return it->get<std::string>();
            }
            return std::string{};
        }

        // The `required` array of an object schema as a set (empty when absent).
        std::set<std::string>
        required_set(nlohmann::json const& schema)
        {
            std::set<std::string> out;
            if (auto const it = schema.find("required"); it != schema.end() && it->is_array())
                for (auto const& k : *it)
                    if (k.is_string())
                        out.insert(k.get<std::string>());
            return out;
        }

        // A sorted JSON array built from a set of names (deterministic output).
        nlohmann::json
        names_to_array(std::set<std::string> const& names)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (auto const& n : names)
                arr.push_back(n);
            return arr;
        }
    } // namespace

    nlohmann::json
    infer_json_schema(nlohmann::json const& sample)
    {
        using nlohmann::json;

        switch (sample.type())
        {
        case json::value_t::object:
        {
            json                  properties = json::object();
            std::set<std::string> keys;
            for (auto const& [k, v] : sample.items())
            {
                properties[k] = infer_json_schema(v);
                keys.insert(k);
            }
            json schema          = json::object();
            schema["type"]       = "object";
            schema["properties"] = std::move(properties);
            schema["required"]   = names_to_array(keys);
            return schema;
        }
        case json::value_t::array:
        {
            json schema    = json::object();
            schema["type"] = "array";
            // Merge the element shapes into a single item schema.
            json items;
            bool have_items = false;
            for (auto const& e : sample)
            {
                json const es = infer_json_schema(e);
                items         = have_items ? merge_json_schema(items, es) : es;
                have_items    = true;
            }
            if (have_items)
                schema["items"] = std::move(items);
            return schema;
        }
        case json::value_t::string:
            return json{{"type", "string"}};
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
            return json{{"type", "integer"}};
        case json::value_t::number_float:
            return json{{"type", "number"}};
        case json::value_t::boolean:
            return json{{"type", "boolean"}};
        case json::value_t::null:
            return json{{"type", "null"}};
        case json::value_t::binary:
        case json::value_t::discarded:
        default:
            return json::object(); // accept-any
        }
    }

    nlohmann::json
    merge_json_schema(nlohmann::json const& a, nlohmann::json const& b)
    {
        using nlohmann::json;

        std::string const ta = schema_type(a);
        std::string const tb = schema_type(b);

        // Either side already accept-any (no type) -> accept-any.
        if (ta.empty() || tb.empty())
            return json::object();

        // Integer/number widen to number.
        if ((ta == "integer" && tb == "number") || (ta == "number" && tb == "integer"))
            return json{{"type", "number"}};

        // Differing types -> accept-any.
        if (ta != tb)
            return json::object();

        if (ta == "object")
        {
            // Union the property schemas; merge schemas for shared keys.
            json merged_props = json::object();

            json const& pa = a.contains("properties") ? a["properties"] : json::object();
            json const& pb = b.contains("properties") ? b["properties"] : json::object();

            std::set<std::string> all_keys;
            for (auto const& [k, v] : pa.items())
                all_keys.insert(k);
            for (auto const& [k, v] : pb.items())
                all_keys.insert(k);

            for (auto const& k : all_keys)
            {
                bool const in_a = pa.contains(k);
                bool const in_b = pb.contains(k);
                if (in_a && in_b)
                    merged_props[k] = merge_json_schema(pa[k], pb[k]);
                else if (in_a)
                    merged_props[k] = pa[k];
                else
                    merged_props[k] = pb[k];
            }

            // required = intersection (a field absent from any sample is optional).
            std::set<std::string> const ra = required_set(a);
            std::set<std::string> const rb = required_set(b);
            std::set<std::string>       req;
            std::set_intersection(ra.begin(), ra.end(), rb.begin(), rb.end(),
                                  std::inserter(req, req.begin()));

            json schema          = json::object();
            schema["type"]       = "object";
            schema["properties"] = std::move(merged_props);
            schema["required"]   = names_to_array(req);
            return schema;
        }

        if (ta == "array")
        {
            json schema    = json::object();
            schema["type"] = "array";
            bool const ia  = a.contains("items");
            bool const ib  = b.contains("items");
            if (ia && ib)
                schema["items"] = merge_json_schema(a["items"], b["items"]);
            else if (ia)
                schema["items"] = a["items"];
            else if (ib)
                schema["items"] = b["items"];
            return schema;
        }

        // Same scalar type.
        return json{{"type", ta}};
    }

    nlohmann::json
    infer_json_schema(std::vector<nlohmann::json> const& samples)
    {
        if (samples.empty())
            return nlohmann::json::object(); // accept-any

        nlohmann::json schema = infer_json_schema(samples.front());
        for (std::size_t i = 1; i < samples.size(); ++i)
            schema = merge_json_schema(schema, infer_json_schema(samples[i]));
        return schema;
    }
}
