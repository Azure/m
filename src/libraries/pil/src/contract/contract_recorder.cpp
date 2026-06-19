// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "contract/contract_recorder.h"

#include <algorithm>
#include <cctype>
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

    namespace
    {
        std::string
        ascii_lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        std::string
        ascii_upper(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        // The clean path: everything before the first '?' (query stripped).
        std::string
        clean_path(std::string_view path)
        {
            auto const q = path.find('?');
            return std::string(q == std::string_view::npos ? path : path.substr(0, q));
        }

        // Case-insensitive header lookup; returns the value or "" when absent.
        std::string
        header_value(std::vector<recorder_header> const& headers, std::string_view name)
        {
            std::string const want = ascii_lower(name);
            for (auto const& [k, v] : headers)
                if (ascii_lower(k) == want)
                    return v;
            return std::string{};
        }

        // The media type from a Content-Type value (parameters after ';' dropped,
        // lowercased, trimmed).
        std::string
        media_type_of(std::string_view content_type)
        {
            auto const semi = content_type.find(';');
            std::string_view mt =
                semi == std::string_view::npos ? content_type : content_type.substr(0, semi);
            // Trim surrounding ASCII whitespace.
            while (!mt.empty() && std::isspace(static_cast<unsigned char>(mt.front())))
                mt.remove_prefix(1);
            while (!mt.empty() && std::isspace(static_cast<unsigned char>(mt.back())))
                mt.remove_suffix(1);
            return ascii_lower(mt);
        }

        bool
        is_json_media(std::string_view media)
        {
            // application/json, application/<x>+json, etc.
            return media.find("json") != std::string_view::npos;
        }

        // Volatile transport/hop headers that must not become part of the derived
        // contract (the HTTP stack, mwin32, or a proxy may add/remove them).
        bool
        is_transport_header(std::string_view lower_name)
        {
            static constexpr std::string_view denylist[] = {
                "content-length", "date", "connection", "keep-alive",
                "transfer-encoding", "server", "host"};
            for (auto const& d : denylist)
                if (lower_name == d)
                    return true;
            return false;
        }

        // Significant header names (lowercased) of a message, excluding transport
        // headers, as a set for intersection.
        std::set<std::string>
        significant_header_names(std::vector<recorder_header> const& headers)
        {
            std::set<std::string> out;
            for (auto const& [k, v] : headers)
            {
                std::string const lk = ascii_lower(k);
                if (!is_transport_header(lk))
                    out.insert(lk);
            }
            return out;
        }

        // Intersect `acc` (optional) with `next`. First call initializes.
        void
        intersect_into(std::optional<std::set<std::string>>& acc, std::set<std::string> const& next)
        {
            if (!acc)
            {
                acc = next;
                return;
            }
            std::set<std::string> result;
            std::set_intersection(acc->begin(), acc->end(), next.begin(), next.end(),
                                  std::inserter(result, result.begin()));
            *acc = std::move(result);
        }

        // Split a clean path into non-empty segments ("/pets/1" -> {"pets","1"}),
        // mirroring how load_openapi_model populates openapi_operation::segments
        // (the path matcher requires it).
        std::vector<std::string>
        split_segments(std::string_view path)
        {
            std::vector<std::string> segs;
            std::size_t              i = 0;
            while (i < path.size())
            {
                while (i < path.size() && path[i] == '/')
                    ++i;
                std::size_t const start = i;
                while (i < path.size() && path[i] != '/')
                    ++i;
                if (i > start)
                    segs.emplace_back(path.substr(start, i - start));
            }
            return segs;
        }
    } // namespace

    void
    http_contract_recorder::observe_request(std::string_view                    method,
                                            std::string_view                    path,
                                            std::vector<recorder_header> const& headers,
                                            std::string_view                    body)
    {
        auto& op = m_operations[{ascii_upper(method), clean_path(path)}];

        if (body.empty())
            return;

        std::string const media = media_type_of(header_value(headers, "content-type"));
        if (media.empty() || is_json_media(media))
        {
            try
            {
                nlohmann::json const parsed = nlohmann::json::parse(body);
                op.has_request_body = true;
                op.request_json_bodies.push_back(parsed);
                return;
            }
            catch (nlohmann::json::parse_error const&)
            {
                // Not JSON after all; fall through to media-type-only recording.
            }
        }

        op.has_request_body = true;
        op.request_media_types.insert(media.empty() ? "application/octet-stream" : media);
    }

    void
    http_contract_recorder::observe_response(std::string_view                    method,
                                             std::string_view                    path,
                                             std::uint16_t                       status,
                                             std::vector<recorder_header> const& headers,
                                             std::string_view                    body)
    {
        auto& op   = m_operations[{ascii_upper(method), clean_path(path)}];
        auto& resp = op.responses[status];

        intersect_into(resp.required_headers, significant_header_names(headers));

        if (body.empty())
            return;

        std::string const media = media_type_of(header_value(headers, "content-type"));
        if (media.empty() || is_json_media(media))
        {
            try
            {
                resp.json_bodies.push_back(nlohmann::json::parse(body));
                return;
            }
            catch (nlohmann::json::parse_error const&)
            {
                // Not JSON; record media type only.
            }
        }

        resp.media_types.insert(media.empty() ? "application/octet-stream" : media);
    }

    openapi_model
    http_contract_recorder::build_model() const
    {
        openapi_model model;
        model.version = openapi_version::v3_0;

        for (auto const& [key, acc] : m_operations)
        {
            openapi_operation op;
            op.method        = key.first;
            op.path_template = key.second;
            op.segments      = split_segments(key.second);

            // Request body.
            if (acc.has_request_body)
            {
                op.has_request_body = true;
                if (!acc.request_json_bodies.empty())
                {
                    nlohmann::json const schema = infer_json_schema(acc.request_json_bodies);
                    op.request_body_schema      = schema;
                    op.request_body_content["application/json"] = openapi_media_body{schema, {}};
                }
                for (auto const& media : acc.request_media_types)
                    op.request_body_content.emplace(media, openapi_media_body{});
            }

            // Responses.
            for (auto const& [status, racc] : acc.responses)
            {
                openapi_response r;
                if (!racc.json_bodies.empty())
                {
                    nlohmann::json const schema = infer_json_schema(racc.json_bodies);
                    r.body_schema               = schema;
                    r.content["application/json"] = openapi_media_body{schema, {}};
                }
                for (auto const& media : racc.media_types)
                    r.content.emplace(media, openapi_media_body{});

                if (racc.required_headers)
                    for (auto const& h : *racc.required_headers)
                        r.required_headers.push_back(h);

                op.responses.emplace(static_cast<int>(status), std::move(r));
            }

            model.operations.push_back(std::move(op));
        }

        return model;
    }

    std::string
    http_contract_recorder::emit_spec() const
    {
        return emit_openapi_yaml(build_model());
    }
}
