// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "http_contract_provider.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "contract_example_driver.h"
#include "openapi_model.h"

namespace m::pil
{
    namespace
    {
        using nlohmann::json;
        using nlohmann::json_schema::json_validator;

        //
        // Sentinel status key under which an operation's "default" response
        // validator is stored. Real HTTP status codes are positive, so a
        // negative sentinel never collides with a declared status.
        //
        constexpr int default_response_status_key = -1;

        //
        // ASCII lower-case a copy of `s` (header names / media types are
        // compared case-insensitively).
        //
        std::string
        ascii_lower(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        //
        // Trim ASCII whitespace from both ends of `s`.
        //
        std::string_view
        trim(std::string_view s)
        {
            std::size_t b = 0;
            std::size_t e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
                ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
                --e;
            return s.substr(b, e - b);
        }

        //
        // Find a header value by case-insensitive name. Returns nullopt when the
        // header is absent.
        //
        std::optional<std::string>
        find_header(std::span<http_header const> headers, std::string_view name)
        {
            std::string const want = ascii_lower(name);
            for (auto const& [k, v] : headers)
                if (ascii_lower(k) == want)
                    return v;
            return std::nullopt;
        }

        //
        // The media type from a Content-Type header value, lower-cased and with
        // any parameters (`; charset=...`) stripped. Empty when absent.
        //
        std::string
        content_media_type(std::span<http_header const> headers)
        {
            auto const ct = find_header(headers, "content-type");
            if (!ct)
                return {};
            std::string_view v = *ct;
            if (auto const semi = v.find(';'); semi != std::string_view::npos)
                v = v.substr(0, semi);
            return ascii_lower(trim(v));
        }

        //
        // PIL's specification of "is this a JSON body" (Design Autonomy): the
        // canonical `application/json` or any `+json` structured-suffix type.
        //
        bool
        is_json_media_type(std::string_view media)
        {
            if (media == "application/json")
                return true;
            return media.size() > 5 && media.substr(media.size() - 5) == "+json";
        }

        //
        // Parse a query string ("a=b&c=d", no leading '?') into ordered key
        // names. Percent-decoding is not performed (keys are compared literally,
        // matching the matcher's discriminator handling).
        //
        std::vector<std::string>
        query_keys(std::string_view path)
        {
            std::vector<std::string> keys;
            auto const               q = path.find('?');
            if (q == std::string_view::npos)
                return keys;
            std::string_view query = path.substr(q + 1);
            if (auto const f = query.find('#'); f != std::string_view::npos)
                query = query.substr(0, f);

            std::size_t pos = 0;
            while (pos <= query.size())
            {
                auto const amp = query.find('&', pos);
                auto const end = (amp == std::string_view::npos) ? query.size() : amp;
                std::string_view pair = query.substr(pos, end - pos);
                if (!pair.empty())
                {
                    auto const eq = pair.find('=');
                    keys.emplace_back(pair.substr(0, eq));
                }
                if (amp == std::string_view::npos)
                    break;
                pos = amp + 1;
            }
            return keys;
        }

        //
        // Try to build a JSON Schema validator for `schema`. Returns nullptr when
        // `schema` is not a usable JSON Schema object, or when the validator
        // cannot compile it. A schema the validator cannot compile degrades to
        // "no body check" rather than failing the whole load — D-HWC-8 states
        // body validation may be loose where the validator's keyword coverage
        // lags; the gap is in the dependency, not in PIL's specification.
        //
        std::shared_ptr<json_validator>
        try_build_validator(json const& schema)
        {
            if (schema.is_null() || !schema.is_object())
                return nullptr;
            try
            {
                auto v = std::make_shared<json_validator>();
                v->set_root_schema(schema);
                return v;
            }
            catch (std::exception const&)
            {
                return nullptr;
            }
        }

        //
        // The JSON request-body schema for an operation, or null json when the
        // operation declares no `application/json` request body.
        //
        json const&
        request_json_schema(openapi_operation const& op)
        {
            static json const null_schema; // null
            auto const        it = op.request_body_content.find("application/json");
            if (it != op.request_body_content.end())
                return it->second.schema;
            return null_schema;
        }

        //
        // The JSON response-body schema for a response, or null json when the
        // response declares no `application/json` body.
        //
        json const&
        response_json_schema(openapi_response const& resp)
        {
            static json const null_schema; // null
            auto const        it = resp.content.find("application/json");
            if (it != resp.content.end())
                return it->second.schema;
            return null_schema;
        }

        //
        // Run a built validator against `body` (raw bytes). Returns true when the
        // body conforms; false when it fails to parse as JSON or fails the schema.
        //
        bool
        body_conforms(json_validator const& validator, std::span<std::uint8_t const> body)
        {
            json instance;
            try
            {
                instance = json::parse(body.begin(), body.end());
            }
            catch (std::exception const&)
            {
                return false; // not parseable JSON -> body-schema violation
            }
            try
            {
                validator.validate(instance);
            }
            catch (std::exception const&)
            {
                return false; // failed the schema
            }
            return true;
        }

        //--------------------------------------------------------------------------
        // live_contract_document — a loaded spec backed by openapi_model
        //--------------------------------------------------------------------------

        class live_contract_document final : public ihttp_contract_document
        {
        public:
            explicit live_contract_document(openapi_model model): m_model(std::move(model))
            {
                build_validators();
            }

            using ihttp_contract_document::validate_request;
            using ihttp_contract_document::validate_response;

            validate_request_disposition
            validate_request(std::string_view              method,
                             std::string_view              path,
                             std::span<http_header const>  headers,
                             std::span<std::uint8_t const> body,
                             std::error_code&              ec) override
            {
                ec.clear();

                auto const match = match_operation(m_model, method, path);
                if (!match)
                    return validate_request_result_code::unknown_operation;

                openapi_operation const& op = *match->operation;

                // Operations the spec marks not-eligible are skipped (D-HWC-9).
                if (!op.validation_eligible)
                    return {};

                // Parameter checks: required query / header parameters must be
                // present. Path parameters are guaranteed present by the match;
                // cookies are not checked.
                std::vector<std::string> const present_query = query_keys(path);
                for (auto const& param : op.parameters)
                {
                    if (!param.required)
                        continue;
                    if (param.location == parameter_location::query)
                    {
                        if (std::find(present_query.begin(), present_query.end(), param.name) ==
                            present_query.end())
                            return validate_request_result_code::parameter_invalid;
                    }
                    else if (param.location == parameter_location::header)
                    {
                        if (!find_header(headers, param.name))
                            return validate_request_result_code::parameter_invalid;
                    }
                }

                // Request body schema (JSON content only).
                if (op.has_request_body && !body.empty())
                {
                    std::string media = content_media_type(headers);
                    if (media.empty())
                        media = "application/json";
                    if (is_json_media_type(media))
                    {
                        if (auto const it = m_request_validators.find(&op);
                            it != m_request_validators.end() && it->second)
                        {
                            if (!body_conforms(*it->second, body))
                                return validate_request_result_code::body_schema_invalid;
                        }
                    }
                }

                return {};
            }

            validate_response_disposition
            validate_response(std::string_view              method,
                              std::string_view              path,
                              std::uint16_t                 status,
                              std::span<http_header const>  headers,
                              std::span<std::uint8_t const> body,
                              std::error_code&              ec) override
            {
                ec.clear();

                auto const match = match_operation(m_model, method, path);
                if (!match)
                    return validate_response_result_code::unknown_operation;

                openapi_operation const& op = *match->operation;

                if (!op.validation_eligible)
                    return {};

                // Status lookup: a literal status, else the default response.
                openapi_response const* resp     = nullptr;
                int                     resp_key  = static_cast<int>(status);
                if (auto const it = op.responses.find(static_cast<int>(status));
                    it != op.responses.end())
                {
                    resp = &it->second;
                }
                else if (op.default_response)
                {
                    resp     = &*op.default_response;
                    resp_key = default_response_status_key;
                }

                if (!resp)
                    return validate_response_result_code::undeclared_status;

                // Response body schema (JSON content only).
                if (!body.empty())
                {
                    std::string media = content_media_type(headers);
                    if (media.empty())
                        media = "application/json";
                    if (is_json_media_type(media))
                    {
                        if (auto const oit = m_response_validators.find(&op);
                            oit != m_response_validators.end())
                        {
                            if (auto const sit = oit->second.find(resp_key);
                                sit != oit->second.end() && sit->second)
                            {
                                if (!body_conforms(*sit->second, body))
                                    return validate_response_result_code::body_schema_invalid;
                            }
                        }
                    }
                }

                // Declared-header presence.
                for (auto const& header_name : resp->required_headers)
                {
                    if (!find_header(headers, header_name))
                        return validate_response_result_code::missing_header;
                }

                return {};
            }

            //
            // Drive support (EXPOSE-2): synthesize one request per operation from
            // the model's examples. Delegates to the model-level synthesizer.
            //
            std::vector<synthesized_request>
            synthesize_requests() const override
            {
                return synthesize_contract_requests(m_model);
            }

        private:
            //
            // Build one validator per JSON body schema in the model. Pointers into
            // m_model.operations are stable for the document's lifetime, so they
            // key the validator maps.
            //
            void
            build_validators()
            {
                for (auto const& op : m_model.operations)
                {
                    if (op.has_request_body)
                    {
                        if (auto v = try_build_validator(request_json_schema(op)))
                            m_request_validators.emplace(&op, std::move(v));
                    }

                    for (auto const& [status, resp] : op.responses)
                    {
                        if (auto v = try_build_validator(response_json_schema(resp)))
                            m_response_validators[&op].emplace(status, std::move(v));
                    }

                    if (op.default_response)
                    {
                        if (auto v = try_build_validator(response_json_schema(*op.default_response)))
                            m_response_validators[&op].emplace(default_response_status_key,
                                                               std::move(v));
                    }
                }
            }

            openapi_model m_model;
            std::map<openapi_operation const*, std::shared_ptr<json_validator>> m_request_validators;
            std::map<openapi_operation const*, std::map<int, std::shared_ptr<json_validator>>>
                m_response_validators;
        };

        //--------------------------------------------------------------------------
        // live_contract_provider — parses spec bytes into a document
        //--------------------------------------------------------------------------

        class live_contract_provider final : public ihttp_contract
        {
        public:
            explicit live_contract_provider(ref_resolver resolver): m_resolver(std::move(resolver))
            {
            }

            using ihttp_contract::load;

            load_disposition
            load(load_flags,
                 std::string_view                          spec_bytes,
                 std::unique_ptr<ihttp_contract_document>& returned_document,
                 std::error_code&                          ec) override
            {
                returned_document.reset();
                ec.clear();

                try
                {
                    openapi_model model = load_openapi_model(spec_bytes, m_resolver);
                    returned_document =
                        std::make_unique<live_contract_document>(std::move(model));
                }
                catch (std::exception const&)
                {
                    // A malformed spec (or an unresolvable ref) is an operational
                    // failure reported through ec, never a contract violation.
                    ec = std::make_error_code(std::errc::invalid_argument);
                }

                return {};
            }

        private:
            ref_resolver m_resolver;
        };
    } // namespace

    std::unique_ptr<ihttp_contract>
    make_http_contract_provider(ref_resolver resolver)
    {
        return std::make_unique<live_contract_provider>(std::move(resolver));
    }
} // namespace m::pil
