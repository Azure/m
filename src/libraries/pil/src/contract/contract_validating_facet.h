// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>

#include <m/pil/http_contract_interfaces.h>
#include <m/utility/enum_operations.h>

#include "contract_errors.h"

//
// Validating decorator facet for the HWC HTTP contract surface
// (M-HWC-CONTRACT-VALIDATE-2, D-HWC-8, D6). It is the contract sibling of the
// logging facet: as each request / response crosses the synthetic HTTP edge it
// invokes the bound contract document and **traces** any violation as a side
// diagnostic. Per D6 it persists nothing.
//
// Surfacing is opt-in. Off by default the facet only traces and always returns
// a success `error_code`, so a validate binding never changes the engine's
// behavior. With `surface_violations` set it additionally returns a
// `contract_error` so a test (or a strict caller) can assert the violation.
// Operational failures from the document (a malformed-spec `error_code`) always
// surface regardless of the flag — they are not contract violations.
//
// The facet acts on the generic message shape (method / path / headers / body /
// status) shared by `synthetic_http_request` / `captured_http_response`, so a
// thin edge adapter can feed messages straight through.
//
// This header is internal to m_pil (lives under src/, not include/).
//

namespace m::pil
{
    class contract_validating_facet
    {
    public:
        enum class flags : std::uint32_t
        {
            // Return a contract_error when a violation is detected (otherwise the
            // facet only traces and returns success).
            surface_violations = 1u << 0,
        };

        contract_validating_facet(std::shared_ptr<ihttp_contract_document> document,
                                  flags                                    facet_flags = {}) noexcept:
            m_document(std::move(document)), m_flags(facet_flags)
        {
        }

        //
        // Validate a request crossing the edge. Returns an empty error_code when
        // conforming (or when surfacing is off); a `contract_error::request_violation`
        // when a violation is detected and surfacing is on; or the document's
        // operational error_code when validation could not run.
        //
        std::error_code
        on_request(std::string_view              method,
                   std::string_view              path,
                   std::span<http_header const>  headers,
                   std::span<std::uint8_t const> body);

        //
        // Validate a response crossing the edge. Returns an empty error_code when
        // conforming (or when surfacing is off); a `contract_error::response_violation`
        // when a violation is detected and surfacing is on; or the document's
        // operational error_code when validation could not run.
        //
        std::error_code
        on_response(std::string_view              method,
                    std::string_view              path,
                    std::uint16_t                 status,
                    std::span<http_header const>  headers,
                    std::span<std::uint8_t const> body);

    private:
        std::shared_ptr<ihttp_contract_document> m_document;
        flags                                    m_flags;
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(contract_validating_facet::flags);
}
