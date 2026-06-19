// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "contract_example_driver.h"

#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "openapi_model.h"

namespace m::pil
{
    namespace
    {
        using nlohmann::json;

        //
        // Render a JSON scalar as a parameter string (path / query / header
        // value). A JSON string yields its raw text (no surrounding quotes); any
        // other scalar yields its compact JSON form (e.g. 7, true).
        //
        std::string
        json_to_param_string(json const& v)
        {
            if (v.is_string())
                return v.get<std::string>();
            if (v.is_null())
                return {};
            return v.dump();
        }

        //
        // A minimal value that conforms to `schema` (DRIVE-1 fallback when no
        // example is authored). Honors `example` / `default` / `enum` first, then
        // synthesizes by `type`: an object carrying its required properties, an
        // empty array, an empty string, zero, or false. Returns null json when
        // the schema gives nothing to go on.
        //
        json
        default_for_schema(json const& schema)
        {
            if (!schema.is_object())
                return json(nullptr);

            if (auto const it = schema.find("example"); it != schema.end())
                return *it;
            if (auto const it = schema.find("default"); it != schema.end())
                return *it;
            if (auto const it = schema.find("enum");
                it != schema.end() && it->is_array() && !it->empty())
                return it->front();

            std::string type;
            if (auto const it = schema.find("type"); it != schema.end())
            {
                if (it->is_string())
                    type = it->get<std::string>();
                else if (it->is_array() && !it->empty() && it->front().is_string())
                    type = it->front().get<std::string>();
            }
            if (type.empty() && schema.contains("properties"))
                type = "object";

            if (type == "object")
            {
                json       obj   = json::object();
                auto const props = schema.find("properties");
                auto const req   = schema.find("required");
                if (req != schema.end() && req->is_array())
                {
                    for (auto const& r : *req)
                    {
                        if (!r.is_string())
                            continue;
                        std::string const name = r.get<std::string>();
                        json sub = (props != schema.end() && props->is_object() &&
                                    props->contains(name))
                                       ? default_for_schema((*props)[name])
                                       : json(nullptr);
                        obj[name] = std::move(sub);
                    }
                }
                return obj;
            }
            if (type == "array")
                return json::array();
            if (type == "string")
                return json("");
            if (type == "integer" || type == "number")
                return json(0);
            if (type == "boolean")
                return json(false);

            return json(nullptr);
        }

        //
        // The string value for a non-body parameter: its authored example, else a
        // schema-derived default. Path / query / header values must be non-empty
        // to form a usable message, so an empty result becomes a placeholder.
        //
        std::string
        parameter_value(openapi_parameter const& p)
        {
            if (!p.example.is_null())
                return json_to_param_string(p.example);
            std::string s = json_to_param_string(default_for_schema(p.schema));
            if (s.empty())
                s = "sample";
            return s;
        }
    } // namespace

    std::vector<synthesized_request>
    synthesize_contract_requests(openapi_model const& model)
    {
        std::vector<synthesized_request> out;
        out.reserve(model.operations.size());

        for (auto const& op : model.operations)
        {
            synthesized_request req;
            req.method = op.method;

            // Path: substitute each "{name}" capture with its path-parameter value.
            std::map<std::string, std::string> path_values;
            for (auto const& p : op.parameters)
                if (p.location == parameter_location::path)
                    path_values.emplace(p.name, parameter_value(p));

            std::string path;
            for (auto const& seg : op.segments)
            {
                path += '/';
                if (seg.size() >= 2 && seg.front() == '{' && seg.back() == '}')
                {
                    std::string const name = seg.substr(1, seg.size() - 2);
                    auto const        it   = path_values.find(name);
                    path += (it != path_values.end()) ? it->second : std::string("sample");
                }
                else
                {
                    path += seg;
                }
            }
            if (op.segments.empty())
                path = "/";

            // Query: every required query parameter (discriminators included) and
            // any example-bearing optional one.
            std::string query;
            for (auto const& p : op.parameters)
            {
                if (p.location != parameter_location::query)
                    continue;
                if (!p.required && p.example.is_null())
                    continue;
                query += query.empty() ? '?' : '&';
                query += p.name + "=" + parameter_value(p);
            }

            req.path = path + query;

            // Declared header parameters.
            for (auto const& p : op.parameters)
            {
                if (p.location != parameter_location::header)
                    continue;
                if (!p.required && p.example.is_null())
                    continue;
                req.headers.emplace_back(p.name, parameter_value(p));
            }

            // Request body. Prefer the authored JSON example, else a schema
            // default; for non-JSON bodies use a string example when present.
            if (op.has_request_body)
            {
                auto const json_it = op.request_body_content.find("application/json");
                if (json_it != op.request_body_content.end())
                {
                    json const body_json = !op.request_body_example.is_null()
                                               ? op.request_body_example
                                               : default_for_schema(json_it->second.schema);
                    if (!body_json.is_null())
                    {
                        std::string const dumped = body_json.dump();
                        req.body.assign(dumped.begin(), dumped.end());
                        req.headers.emplace_back("Content-Type", "application/json");
                    }
                }
                else
                {
                    for (auto const& [media, mb] : op.request_body_content)
                    {
                        if (mb.example.is_string())
                        {
                            std::string const s = mb.example.get<std::string>();
                            req.body.assign(s.begin(), s.end());
                            req.headers.emplace_back("Content-Type", media);
                            break;
                        }
                    }
                }
            }

            out.push_back(std::move(req));
        }

        return out;
    }

    drive_tally
    drive_contract(std::vector<synthesized_request> const& requests,
                   engine_submit const&                    submit,
                   ihttp_contract_document*                validator)
    {
        drive_tally tally;

        for (auto const& req : requests)
        {
            ++tally.requests;

            captured_contract_response const resp = submit(req);

            if (validator)
            {
                std::error_code ec;
                auto const      d = validator->validate_response(
                    req.method, req.path, resp.status, resp.headers, resp.body, ec);
                ++tally.responses_validated;

                // An operational failure (ec) is neither conforming nor a
                // contract violation; only a clean validation result is tallied.
                if (!ec)
                {
                    if (d)
                        ++tally.violating;
                    else
                        ++tally.conforming;
                }
            }
        }

        return tally;
    }

    //
    // Public drive entry point (EXPOSE-2): synthesize from the document, submit,
    // and validate each response against that same document. A thin wrapper over
    // the model-level synthesizer (via the document) and the lower-level driver.
    //
    drive_tally
    drive_contract(ihttp_contract_document& document, engine_submit const& submit)
    {
        auto const requests = document.synthesize_requests();
        return drive_contract(requests, submit, &document);
    }
} // namespace m::pil
