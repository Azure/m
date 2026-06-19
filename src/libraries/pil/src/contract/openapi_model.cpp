// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "openapi_model.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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
        // The directory portion of a bundle-relative document key. "" (the root)
        // and a bare filename both have an empty directory.
        //
        std::string
        dir_of(std::string_view key)
        {
            auto const slash = key.rfind('/');
            if (slash == std::string_view::npos)
                return std::string{};
            return std::string(key.substr(0, slash));
        }

        //
        // Join a `$ref` file part against the directory of the referring document
        // and normalize "." / ".." segments. Produces the bundle-relative key used
        // both to look up the document and as the resolver argument.
        //
        std::string
        join_rel(std::string_view base_dir, std::string_view rel)
        {
            std::vector<std::string> parts;
            auto const               push_path = [&](std::string_view p) {
                std::size_t i = 0;
                while (i < p.size())
                {
                    while (i < p.size() && p[i] == '/')
                        ++i;
                    std::size_t const start = i;
                    while (i < p.size() && p[i] != '/')
                        ++i;
                    if (i <= start)
                        continue;
                    std::string_view seg = p.substr(start, i - start);
                    if (seg == ".")
                        continue;
                    if (seg == "..")
                    {
                        if (!parts.empty() && parts.back() != "..")
                            parts.pop_back();
                        else
                            parts.emplace_back("..");
                        continue;
                    }
                    parts.emplace_back(seg);
                }
            };
            push_path(base_dir);
            push_path(rel);

            std::string out;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                if (i != 0)
                    out += '/';
                out += parts[i];
            }
            return out;
        }

        //
        // Resolves and inlines every `$ref` in a document tree. Internal refs
        // (`#/...`) resolve within the current document; relative-file refs
        // (`other.yml#/...`) load the referenced document through the caller's
        // resolver and resolve transitively. References are tracked by absolute
        // key ("doc-key#fragment") so a cycle breaks (an empty object is yielded
        // for the back-edge) instead of recursing forever. An unresolved ref is a
        // diagnostic-by-exception. Sibling members of a `$ref` are ignored, per
        // the JSON Reference rule.
        //
        class ref_inliner
        {
        public:
            ref_inliner(nlohmann::json root, ref_resolver resolver)
                : resolver_(std::move(resolver))
            {
                documents_.emplace(std::string{}, std::move(root));
            }

            nlohmann::json
            resolve_root()
            {
                std::set<std::string> active;
                // resolve() copies the root out of the cache first to avoid
                // aliasing while it mutates nothing, then walks it.
                nlohmann::json const root = documents_.at(std::string{});
                return resolve(root, std::string{}, active);
            }

        private:
            nlohmann::json const&
            document(std::string const& key)
            {
                if (auto const it = documents_.find(key); it != documents_.end())
                    return it->second;

                if (!resolver_)
                    throw std::runtime_error(
                        "openapi: external $ref '" + key + "' but no resolver was supplied");

                std::optional<std::string> const bytes = resolver_(key);
                if (!bytes.has_value())
                    throw std::runtime_error("openapi: could not resolve referenced document '" +
                                             key + "'");

                nlohmann::json parsed;
                try
                {
                    parsed = yaml_to_json(YAML::Load(*bytes));
                }
                catch (YAML::Exception const& e)
                {
                    throw std::runtime_error("openapi: invalid YAML/JSON in referenced document '" +
                                             key + "': " + e.what());
                }
                return documents_.emplace(key, std::move(parsed)).first->second;
            }

            //
            // Navigate a JSON-pointer fragment ("/a/b") within a document.
            //
            nlohmann::json const&
            at_fragment(nlohmann::json const& doc, std::string const& fragment, std::string const& abs)
            {
                if (fragment.empty())
                    return doc;
                nlohmann::json::json_pointer ptr;
                try
                {
                    ptr = nlohmann::json::json_pointer(fragment);
                }
                catch (...)
                {
                    throw std::runtime_error("openapi: malformed $ref fragment '" + abs + "'");
                }
                if (!doc.contains(ptr))
                    throw std::runtime_error("openapi: $ref target not found '" + abs + "'");
                return doc.at(ptr);
            }

            nlohmann::json
            resolve(nlohmann::json const& node, std::string const& file_key,
                    std::set<std::string>& active)
            {
                if (node.is_object())
                {
                    if (auto const r = node.find("$ref");
                        r != node.end() && r->is_string())
                    {
                        std::string const ref = r->get<std::string>();
                        std::string       file_part;
                        std::string       fragment;
                        if (auto const h = ref.find('#'); h != std::string::npos)
                        {
                            file_part = ref.substr(0, h);
                            fragment  = ref.substr(h + 1);
                        }
                        else
                        {
                            file_part = ref;
                        }

                        std::string const target_key =
                            file_part.empty() ? file_key : join_rel(dir_of(file_key), file_part);
                        std::string const abs = target_key + "#" + fragment;

                        if (active.count(abs) != 0)
                            return nlohmann::json::object(); // cycle back-edge

                        nlohmann::json const& doc    = document(target_key);
                        nlohmann::json const& target = at_fragment(doc, fragment, abs);

                        active.insert(abs);
                        nlohmann::json out = resolve(target, target_key, active);
                        active.erase(abs);
                        return out;
                    }

                    nlohmann::json out = nlohmann::json::object();
                    for (auto const& [k, v] : node.items())
                        out[k] = resolve(v, file_key, active);
                    return out;
                }

                if (node.is_array())
                {
                    nlohmann::json out = nlohmann::json::array();
                    for (auto const& e : node)
                        out.push_back(resolve(e, file_key, active));
                    return out;
                }

                return node;
            }

            ref_resolver                          resolver_;
            std::map<std::string, nlohmann::json> documents_;
        };

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

        //
        // Parse a query string ("a=b&c=d", no leading '?') into ordered key/value
        // pairs. A bare key ("flag") yields an empty value. Percent-decoding is
        // not performed (the discriminators and request keys are compared as
        // authored).
        //
        std::vector<std::pair<std::string, std::string>>
        parse_query(std::string_view q)
        {
            std::vector<std::pair<std::string, std::string>> out;
            std::size_t                                      i = 0;
            while (i < q.size())
            {
                while (i < q.size() && (q[i] == '&' || q[i] == ';'))
                    ++i;
                std::size_t const start = i;
                while (i < q.size() && q[i] != '&' && q[i] != ';')
                    ++i;
                if (i <= start)
                    continue;
                std::string_view const pair = q.substr(start, i - start);
                auto const             eq   = pair.find('=');
                if (eq == std::string_view::npos)
                    out.emplace_back(std::string(pair), std::string{});
                else
                    out.emplace_back(std::string(pair.substr(0, eq)),
                                     std::string(pair.substr(eq + 1)));
            }
            return out;
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
        // Pull an OAS3 `content` map into a media-type -> body map (every media
        // type, JSON and non-JSON alike, per D-HWC-9) and also surface the JSON
        // entry through `schema`/`example` (the convenience view). Prefers
        // application/json, then any media type whose name contains "json".
        //
        void
        extract_all_content(nlohmann::json const&                      obj,
                            std::map<std::string, openapi_media_body>& content,
                            nlohmann::json&                            schema,
                            nlohmann::json&                            example)
        {
            auto const c = obj.find("content");
            if (c == obj.end() || !c->is_object())
                return;

            std::string json_ct;
            for (auto const& [ct, media] : c->items())
            {
                if (!media.is_object())
                    continue;

                openapi_media_body body;
                if (auto const s = media.find("schema"); s != media.end())
                    body.schema = *s;
                if (auto const e = media.find("example"); e != media.end())
                    body.example = *e;
                content[ct] = std::move(body);

                if (ct == "application/json")
                    json_ct = ct;
                else if (json_ct.empty() && ct.find("json") != std::string::npos)
                    json_ct = ct;
            }

            if (!json_ct.empty())
            {
                schema  = content[json_ct].schema;
                example = content[json_ct].example;
            }
        }

        //
        // Map a single OAS2 schema (one `schema` keyword, with an optional
        // example) onto a media-type map under `media_type`, also setting the
        // JSON convenience view when that media type is JSON.
        //
        void
        place_oas2_body(nlohmann::json const&                      schema_in,
                       nlohmann::json const&                      example_in,
                       std::string const&                         media_type,
                       std::map<std::string, openapi_media_body>& content,
                       nlohmann::json&                            schema,
                       nlohmann::json&                            example)
        {
            openapi_media_body body;
            body.schema          = schema_in;
            body.example         = example_in;
            content[media_type]  = std::move(body);
            if (media_type == "application/json" || media_type.find("json") != std::string::npos)
            {
                schema  = schema_in;
                example = example_in;
            }
        }

        //
        // Parse a `parameters` array. Non-body parameters are appended to `out`;
        // an OAS2 `in: body` parameter sets the request body schema instead.
        //
        void
        parse_parameters(nlohmann::json const&                      arr,
                         openapi_version                            version,
                         std::string const&                         req_media,
                         std::vector<openapi_parameter>&            out,
                         bool&                                      has_body,
                         nlohmann::json&                            body_schema,
                         nlohmann::json&                            body_example,
                         std::map<std::string, openapi_media_body>& body_content)
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
                        nlohmann::json ex;
                        if (auto const e = p.find("example"); e != p.end())
                            ex = *e;
                        place_oas2_body(
                            *s, ex, req_media, body_content, body_schema, body_example);
                        has_body = true;
                    }
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
        extract_response_body(nlohmann::json const&                      resp,
                              openapi_version                            version,
                              std::string const&                         resp_media,
                              std::map<std::string, openapi_media_body>& content,
                              nlohmann::json&                            schema)
        {
            if (version == openapi_version::v2_0)
            {
                if (auto const s = resp.find("schema"); s != resp.end())
                {
                    nlohmann::json ex;
                    if (auto const e = resp.find("example"); e != resp.end())
                        ex = *e;
                    place_oas2_body(*s, ex, resp_media, content, schema, ex);
                }
            }
            else
            {
                nlohmann::json example;
                extract_all_content(resp, content, schema, example);
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

        //
        // The first declared media type in an OAS2 `consumes`/`produces` array,
        // or `fallback` when the array is absent or empty. OAS2 has no per-body
        // media type, so the request/response schema is keyed under this.
        //
        std::string
        first_media(nlohmann::json const& container, char const* key, std::string_view fallback)
        {
            auto const it = container.find(key);
            if (it != container.end() && it->is_array())
            {
                for (auto const& m : *it)
                    if (m.is_string())
                        return m.get<std::string>();
            }
            return std::string(fallback);
        }

        //
        // Read the authored `x-validated` eligibility flag from an operation or
        // path item, inheriting the enclosing value when the key is absent
        // (D-HWC-9). Only an explicit boolean overrides the inherited value.
        //
        bool
        read_validation_eligible(nlohmann::json const& obj, bool inherited)
        {
            if (auto const it = obj.find("x-validated"); it != obj.end() && it->is_boolean())
                return it->get<bool>();
            return inherited;
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

    path_key_parts
    normalize_path_key(std::string_view path_key)
    {
        path_key_parts parts;
        auto const     q = path_key.find('?');
        if (q == std::string_view::npos)
        {
            parts.clean_path = std::string(path_key);
            return parts;
        }
        parts.clean_path    = std::string(path_key.substr(0, q));
        parts.discriminators = parse_query(path_key.substr(q + 1));
        return parts;
    }

    openapi_model
    load_openapi_model(std::string_view spec_text, ref_resolver resolver)
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

        nlohmann::json const raw = yaml_to_json(root);
        if (!raw.is_object())
            throw std::runtime_error("openapi: document root must be an object");

        // Resolve the whole $ref bundle up front, so the walk below sees fully
        // inlined parameters, bodies, and responses (D-HWC-9).
        nlohmann::json const doc = ref_inliner(raw, std::move(resolver)).resolve_root();

        openapi_model model;
        model.version = detect_version(doc);

        // OAS2 has no per-body media type; the spec-level consumes/produces give
        // the request/response media type (an operation may override).
        std::string const spec_req_media  = first_media(doc, "consumes", "application/json");
        std::string const spec_resp_media = first_media(doc, "produces", "application/json");

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

            path_key_parts const key = normalize_path_key(path_key);

            // Lift each query discriminator into a required query parameter whose
            // schema fixes the value (an enum of one).
            std::vector<openapi_parameter> discriminator_params;
            for (auto const& [dk, dv] : key.discriminators)
            {
                openapi_parameter dp;
                dp.name     = dk;
                dp.location = parameter_location::query;
                dp.required = true;
                dp.schema   = nlohmann::json{{"enum", nlohmann::json::array({dv})}};
                discriminator_params.push_back(std::move(dp));
            }

            bool const path_eligible = read_validation_eligible(path_item, true);

            // Path-level parameters apply to every operation under the path.
            std::vector<openapi_parameter>            path_params;
            nlohmann::json                            path_body_schema;
            nlohmann::json                            path_body_example;
            std::map<std::string, openapi_media_body> path_body_content;
            bool                                      path_has_body = false;
            if (auto const pit = path_item.find("parameters"); pit != path_item.end())
                parse_parameters(*pit,
                                 model.version,
                                 spec_req_media,
                                 path_params,
                                 path_has_body,
                                 path_body_schema,
                                 path_body_example,
                                 path_body_content);

            for (auto const method : http_methods)
            {
                auto const op_it = path_item.find(std::string(method));
                if (op_it == path_item.end())
                    continue;
                if (!op_it->is_object())
                    throw std::runtime_error("openapi: operation must be an object");

                std::string const req_media  = first_media(*op_it, "consumes", spec_req_media);
                std::string const resp_media = first_media(*op_it, "produces", spec_resp_media);

                openapi_operation op;
                op.method               = ascii_upper(method);
                op.path_template        = key.clean_path;
                op.segments             = split_segments(key.clean_path);
                op.query_discriminators = key.discriminators;
                op.validation_eligible  = read_validation_eligible(*op_it, path_eligible);
                op.parameters           = discriminator_params;
                op.parameters.insert(op.parameters.end(), path_params.begin(), path_params.end());
                op.has_request_body     = path_has_body;
                op.request_body_schema  = path_body_schema;
                op.request_body_example = path_body_example;
                op.request_body_content = path_body_content;

                if (auto const pit = op_it->find("parameters"); pit != op_it->end())
                    parse_parameters(*pit,
                                     model.version,
                                     req_media,
                                     op.parameters,
                                     op.has_request_body,
                                     op.request_body_schema,
                                     op.request_body_example,
                                     op.request_body_content);

                // OAS3 carries the request body in `requestBody`, not a parameter.
                if (model.version != openapi_version::v2_0)
                {
                    if (auto const rb = op_it->find("requestBody");
                        rb != op_it->end() && rb->is_object())
                    {
                        extract_all_content(*rb,
                                           op.request_body_content,
                                           op.request_body_schema,
                                           op.request_body_example);
                        op.has_request_body = !op.request_body_content.empty();
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
                        extract_response_body(
                            resp_obj, model.version, resp_media, r.content, r.body_schema);
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
        // Separate the query string and strip the fragment.
        std::string_view query;
        if (auto const f = path.find('#'); f != std::string_view::npos)
            path = path.substr(0, f);
        if (auto const q = path.find('?'); q != std::string_view::npos)
        {
            query = path.substr(q + 1);
            path  = path.substr(0, q);
        }

        std::vector<std::pair<std::string, std::string>> const req_query = parse_query(query);
        auto const has_query_pair = [&](std::pair<std::string, std::string> const& want) {
            for (auto const& [k, v] : req_query)
                if (k == want.first && v == want.second)
                    return true;
            return false;
        };

        std::vector<std::string> const req_segs   = split_segments(path);
        std::string const              method_up  = ascii_upper(method);

        openapi_path_match best;
        bool               found      = false;
        std::size_t        best_caps  = 0;
        std::size_t        best_discs = 0;

        for (auto const& op : model.operations)
        {
            if (op.method != method_up)
                continue;
            if (op.segments.size() != req_segs.size())
                continue;

            // Every query discriminator on the operation must be present in the
            // request query with the exact value.
            bool discs_ok = true;
            for (auto const& disc : op.query_discriminators)
            {
                if (!has_query_pair(disc))
                {
                    discs_ok = false;
                    break;
                }
            }
            if (!discs_ok)
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

            // Prefer the most specific match: more query discriminators first,
            // then fewer path captures.
            bool const better =
                !found || op.query_discriminators.size() > best_discs ||
                (op.query_discriminators.size() == best_discs && captures.size() < best_caps);
            if (better)
            {
                best.operation       = &op;
                best.path_parameters = std::move(captures);
                best_caps            = best.path_parameters.size();
                best_discs           = op.query_discriminators.size();
                found                = true;
            }
        }

        if (!found)
            return std::nullopt;
        return best;
    }
} // namespace m::pil
