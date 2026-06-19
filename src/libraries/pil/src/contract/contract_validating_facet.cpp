// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "contract_validating_facet.h"

#include <string>
#include <string_view>
#include <system_error>

#include <m/tracing/tracing.h>

namespace m::pil
{
    namespace
    {
        std::string_view
        request_code_name(ihttp_contract_document::validate_request_result_code code)
        {
            using code_t = ihttp_contract_document::validate_request_result_code;
            switch (code)
            {
            case code_t::unknown_operation:
                return "unknown_operation";
            case code_t::parameter_invalid:
                return "parameter_invalid";
            case code_t::body_schema_invalid:
                return "body_schema_invalid";
            }
            return "unknown";
        }

        std::string_view
        response_code_name(ihttp_contract_document::validate_response_result_code code)
        {
            using code_t = ihttp_contract_document::validate_response_result_code;
            switch (code)
            {
            case code_t::unknown_operation:
                return "unknown_operation";
            case code_t::undeclared_status:
                return "undeclared_status";
            case code_t::body_schema_invalid:
                return "body_schema_invalid";
            case code_t::missing_header:
                return "missing_header";
            }
            return "unknown";
        }

        bool
        surfacing(contract_validating_facet::flags flags)
        {
            return (flags & contract_validating_facet::flags::surface_violations) !=
                   contract_validating_facet::flags{};
        }
    } // namespace

    std::error_code
    contract_validating_facet::on_request(std::string_view              method,
                                          std::string_view              path,
                                          std::span<http_header const>  headers,
                                          std::span<std::uint8_t const> body)
    {
        if (!m_document)
            return {};

        std::error_code ec;
        auto const      d = m_document->validate_request(method, path, headers, body, ec);

        if (ec)
        {
            // Operational failure (e.g. could not run validation): always surfaces.
            m::trace(m::tracing::event_kind::error,
                     "contract: request {} {} could not be validated: {}",
                     method,
                     path,
                     ec.message());
            return ec;
        }

        if (d)
        {
            m::trace(m::tracing::event_kind::information,
                     "contract: request {} {} violates contract ({})",
                     method,
                     path,
                     request_code_name(d.code()));
            if (surfacing(m_flags))
                return make_error_code(contract_error::request_violation);
        }

        return {};
    }

    std::error_code
    contract_validating_facet::on_response(std::string_view              method,
                                           std::string_view              path,
                                           std::uint16_t                 status,
                                           std::span<http_header const>  headers,
                                           std::span<std::uint8_t const> body)
    {
        if (!m_document)
            return {};

        std::error_code ec;
        auto const      d = m_document->validate_response(method, path, status, headers, body, ec);

        if (ec)
        {
            m::trace(m::tracing::event_kind::error,
                     "contract: response {} {} ({}) could not be validated: {}",
                     method,
                     path,
                     status,
                     ec.message());
            return ec;
        }

        if (d)
        {
            m::trace(m::tracing::event_kind::information,
                     "contract: response {} {} ({}) violates contract ({})",
                     method,
                     path,
                     status,
                     response_code_name(d.code()));
            if (surfacing(m_flags))
                return make_error_code(contract_error::response_violation);
        }

        return {};
    }
} // namespace m::pil
