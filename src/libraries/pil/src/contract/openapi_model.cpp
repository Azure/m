// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "openapi_model.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

namespace m::pil
{
    namespace
    {
        //
        // Uppercase the ASCII letters of a string (HTTP methods are ASCII).
        //
        std::string
        ascii_upper(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        //
        // Convert a single YAML scalar to a JSON value, inferring its type.
        // Quoted scalars (yaml-cpp tags them with the non-specific "!" tag) are
        // always strings, so a quoted version like "2.0" never collapses to a
        // number.
        //
        nlohmann::json
        scalar_to_json(YAML::Node const& n)
        {
            if (n.Tag() == "!")
                return n.Scalar();

            std::string const s = n.Scalar();
            if (s.empty())
                return std::string{};
            if (s == "null" || s == "~" || s == "Null" || s == "NULL")
                return nullptr;
            if (s == "true" || s == "True" || s == "TRUE")
                return true;
            if (s == "false" || s == "False" || s == "FALSE")
                return false;

            // Integer (whole string must be consumed).
            try
            {
                std::size_t pos = 0;
                long long   v   = std::stoll(s, &pos);
                if (pos == s.size())
                    return v;
            }
            catch (...)
            {
            }

            // Floating point (whole string must be consumed).
            try
            {
                std::size_t pos = 0;
                double      v   = std::stod(s, &pos);
                if (pos == s.size())
                    return v;
            }
            catch (...)
            {
            }

            return s;
        }

        //
        // Recursively convert a yaml-cpp node tree into nlohmann::json.
        //
        nlohmann::json
        yaml_to_json(YAML::Node const& node)
        {
            switch (node.Type())
            {
            case YAML::NodeType::Null:
                return nullptr;
            case YAML::NodeType::Scalar:
                return scalar_to_json(node);
            case YAML::NodeType::Sequence:
            {
                nlohmann::json arr = nlohmann::json::array();
                for (auto const& e : node)
                    arr.push_back(yaml_to_json(e));
                return arr;
            }
            case YAML::NodeType::Map:
            {
                nlohmann::json obj = nlohmann::json::object();
                for (auto const& kv : node)
                    obj[kv.first.as<std::string>()] = yaml_to_json(kv.second);
                return obj;
            }
            case YAML::NodeType::Undefined:
            default:
                return nullptr;
            }
        }

        //
        // Split a path (template or concrete) into non-empty segments.
        //
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

        parameter_location
        parse_location(std::string const& in)
        {
            if (in == "path")
                return parameter_location::path;
            if (in == "header")
                return parameter_location::header;
            if (in == "cookie")
                return parameter_location::cookie;
            return parameter_location::query;
        }

        //
        // Pull the JSON media-type schema/example out of an OAS3 `content` map
        // (used for both requestBody and responses). Prefers application/json,
        // then any media type whose name contains "json".
        //
        void
        extract_json_content(nlohmann::json const& obj,
                             nlohmann::json&       schema,
                             nlohmann::json&       example)
        {
            auto const c = obj.find("content");
            if (c == obj.end() || !c->is_object())
                return;

            nlohmann::json const* chosen = nullptr;
            for (auto const& [ct, media] : c->items())
            {
                if (!media.is_object())
                    continue;
                if (ct == "application/json")
                {
                    chosen = &media;
                    break;
                }
                if (chosen == nullptr && ct.find("json") != std::string::npos)
                    chosen = &media;
            }
            if (chosen == nullptr)
                return;

            if (auto const s = chosen->find("schema"); s != chosen->end())
                schema = *s;
            if (auto const e = chosen->find("example"); e != chosen->end())
                example = *e;
        }

        //
        // Parse a `parameters` array. Non-body parameters are appended to `out`;
        // an OAS2 `in: body` parameter sets the request body schema instead.
        //
        void
        parse_parameters(nlohmann::json const&           arr,
                         openapi_version                 version,
                         std::vector<openapi_parameter>& out,
                         bool&                           has_body,
                         nlohmann::json&                 body_schema,
                         nlohmann::json&                 body_example)
        {
            if (!arr.is_array())
                throw std::runtime_error("openapi: 'parameters' must be an array");

            for (auto const& p : arr)
            {
                if (!p.is_object())
                    continue;

                auto const        in_it = p.find("in");
                std::string const in =
                    (in_it != p.end() && in_it->is_string()) ? in_it->get<std::string>() : "query";

                if (version == openapi_version::v2_0 && in == "body")
                {
                    if (auto const s = p.find("schema"); s != p.end())
                    {
                        body_schema = *s;
                        has_body    = true;
                    }
                    if (auto const e = p.find("example"); e != p.end())
                        body_example = *e;
                    continue;
                }

                openapi_parameter param;
                if (auto const n = p.find("name"); n != p.end() && n->is_string())
                    param.name = n->get<std::string>();
                param.location = parse_location(in);
                if (auto const r = p.find("required"); r != p.end() && r->is_boolean())
                    param.required = r->get<bool>();
                // Path parameters are required by definition.
                if (param.location == parameter_location::path)
                    param.required = true;

                if (auto const s = p.find("schema"); s != p.end())
                {
                    param.schema = *s;
                }
                else if (version == openapi_version::v2_0)
                {
                    // OAS2 inlines the schema keywords on the parameter itself.
                    nlohmann::json sch = nlohmann::json::object();
                    for (char const* k :
                         {"type", "format", "enum", "minimum", "maximum", "pattern", "items"})
                    {
                        if (auto const it = p.find(k); it != p.end())
                            sch[k] = *it;
                    }
                    if (!sch.empty())
                        param.schema = std::move(sch);
                }

                if (auto const e = p.find("example"); e != p.end())
                    param.example = *e;

                out.push_back(std::move(param));
            }
        }

        void
        extract_response_body(nlohmann::json const& resp,
                              openapi_version       version,
                              nlohmann::json&       schema)
        {
            if (version == openapi_version::v2_0)
            {
                if (auto const s = resp.find("schema"); s != resp.end())
                    schema = *s;
            }
            else
            {
                nlohmann::json example;
                extract_json_content(resp, schema, example);
            }
        }

        void
        extract_required_headers(nlohmann::json const& resp, std::vector<std::string>& out)
        {
            auto const h = resp.find("headers");
            if (h == resp.end() || !h->is_object())
                return;
            for (auto const& [name, def] : h->items())
            {
                bool req = false;
                if (def.is_object())
                {
                    if (auto const r = def.find("required"); r != def.end() && r->is_boolean())
                        req = r->get<bool>();
                }
                if (req)
                    out.push_back(name);
            }
        }

        openapi_version
        detect_version(nlohmann::json const& doc)
        {
            if (auto const it = doc.find("openapi"); it != doc.end())
            {
                std::string const v = it->is_string() ? it->get<std::string>() : it->dump();
                if (v.rfind("3.1", 0) == 0)
                    return openapi_version::v3_1;
                if (v.rfind("3", 0) == 0)
                    return openapi_version::v3_0;
                throw std::runtime_error("openapi: unsupported 'openapi' version: " + v);
            }
            if (auto const it = doc.find("swagger"); it != doc.end())
            {
                std::string const v = it->is_string() ? it->get<std::string>() : it->dump();
                if (v == "2.0")
                    return openapi_version::v2_0;
                throw std::runtime_error("openapi: unsupported 'swagger' version: " + v);
            }
            throw std::runtime_error("openapi: missing 'openapi' or 'swagger' version field");
        }
    } // namespace

    openapi_model
    load_openapi_model(std::string_view spec_text)
    {
        YAML::Node root;
        try
        {
            root = YAML::Load(std::string(spec_text));
        }
        catch (YAML::Exception const& e)
        {
            throw std::runtime_error(std::string("openapi: invalid YAML/JSON: ") + e.what());
        }

        nlohmann::json const doc = yaml_to_json(root);
        if (!doc.is_object())
            throw std::runtime_error("openapi: document root must be an object");

        openapi_model model;
        model.version = detect_version(doc);

        auto const paths_it = doc.find("paths");
        if (paths_it == doc.end())
            return model; // A spec with no paths is valid (an empty model).
        if (!paths_it->is_object())
            throw std::runtime_error("openapi: 'paths' must be an object");

        static constexpr std::array<std::string_view, 8> http_methods = {
            "get", "put", "post", "delete", "patch", "head", "options", "trace"};

        for (auto const& [path_key, path_item] : paths_it->items())
        {
            if (!path_item.is_object())
                throw std::runtime_error("openapi: path item must be an object: " + path_key);

            // Path-level parameters apply to every operation under the path.
            std::vector<openapi_parameter> path_params;
            nlohmann::json                 path_body_schema;
            nlohmann::json                 path_body_example;
            bool                           path_has_body = false;
            if (auto const pit = path_item.find("parameters"); pit != path_item.end())
                parse_parameters(*pit,
                                 model.version,
                                 path_params,
                                 path_has_body,
                                 path_body_schema,
                                 path_body_example);

            for (auto const method : http_methods)
            {
                auto const op_it = path_item.find(std::string(method));
                if (op_it == path_item.end())
                    continue;
                if (!op_it->is_object())
                    throw std::runtime_error("openapi: operation must be an object");

                openapi_operation op;
                op.method              = ascii_upper(method);
                op.path_template       = path_key;
                op.segments            = split_segments(path_key);
                op.parameters          = path_params;
                op.has_request_body    = path_has_body;
                op.request_body_schema = path_body_schema;
                op.request_body_example = path_body_example;

                if (auto const pit = op_it->find("parameters"); pit != op_it->end())
                    parse_parameters(*pit,
                                     model.version,
                                     op.parameters,
                                     op.has_request_body,
                                     op.request_body_schema,
                                     op.request_body_example);

                // OAS3 carries the request body in `requestBody`, not a parameter.
                if (model.version != openapi_version::v2_0)
                {
                    if (auto const rb = op_it->find("requestBody");
                        rb != op_it->end() && rb->is_object())
                    {
                        extract_json_content(*rb, op.request_body_schema, op.request_body_example);
                        op.has_request_body = !op.request_body_schema.is_null();
                    }
                }

                if (auto const resp = op_it->find("responses");
                    resp != op_it->end() && resp->is_object())
                {
                    for (auto const& [status_key, resp_obj] : resp->items())
                    {
                        if (!resp_obj.is_object())
                            continue;
                        openapi_response r;
                        extract_response_body(resp_obj, model.version, r.body_schema);
                        extract_required_headers(resp_obj, r.required_headers);
                        if (status_key == "default")
                        {
                            op.default_response = std::move(r);
                        }
                        else
                        {
                            try
                            {
                                std::size_t pos  = 0;
                                int const   code = std::stoi(status_key, &pos);
                                if (pos == status_key.size())
                                    op.responses.emplace(code, std::move(r));
                            }
                            catch (...)
                            {
                                // Non-numeric, non-"default" status key: ignore.
                            }
                        }
                    }
                }

                model.operations.push_back(std::move(op));
            }
        }

        return model;
    }

    std::optional<openapi_path_match>
    match_operation(openapi_model const& model, std::string_view method, std::string_view path)
    {
        // Strip query string and fragment.
        if (auto const q = path.find('?'); q != std::string_view::npos)
            path = path.substr(0, q);
        if (auto const f = path.find('#'); f != std::string_view::npos)
            path = path.substr(0, f);

        std::vector<std::string> const req_segs = split_segments(path);
        std::string const              method_up = ascii_upper(method);

        openapi_path_match best;
        bool               found     = false;
        std::size_t        best_caps = 0;

        for (auto const& op : model.operations)
        {
            if (op.method != method_up)
                continue;
            if (op.segments.size() != req_segs.size())
                continue;

            std::vector<std::pair<std::string, std::string>> captures;
            bool                                             ok = true;
            for (std::size_t i = 0; i < op.segments.size(); ++i)
            {
                std::string const& tseg = op.segments[i];
                if (tseg.size() >= 2 && tseg.front() == '{' && tseg.back() == '}')
                {
                    captures.emplace_back(tseg.substr(1, tseg.size() - 2), req_segs[i]);
                }
                else if (tseg != req_segs[i])
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;

            // Prefer the most specific match (fewest captures).
            if (!found || captures.size() < best_caps)
            {
                best.operation       = &op;
                best.path_parameters = std::move(captures);
                best_caps            = best.path_parameters.size();
                found                = true;
            }
        }

        if (!found)
            return std::nullopt;
        return best;
    }
} // namespace m::pil
