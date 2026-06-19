// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/http_contract.h>
#include <m/utility/enum_operations.h>
#include <m/utility/error_macros.h>

//
// Interface (provider) layer for the HTTP contract isolation surface
// (D-HWC-8, D-HWC-9). A "contract" is an OpenAPI/Swagger spec bound to the
// synthetic HTTP edge. The surface lets a provider load a spec into a
// validating document and then check requests and responses crossing the edge
// against it.
//
// Behavior is owned by PIL (Design Autonomy): an operation is selected by
// matching an HTTP message's method + path (plus any query discriminator)
// against the spec's templated paths; path/query/header parameters and — for
// JSON content — the body schema are validated; for a response the status code
// must be declared and its body/headers validated. The JSON-Schema check is
// delegated to a validator chosen because its behavior matches that spec; the
// matching is code PIL owns.
//
// Error model: like iwebcore / ihttp_listener, the std::error_code& channel is
// the non-throwing primitive (it reports operational failures such as a
// malformed spec). The returned `disposition` carries only contractual
// non-success — the violation kinds. A conforming message yields a false
// disposition and no error. The null provider reports not-implemented.
//

namespace m::pil
{
    //--------------------------------------------------------------------------
    // http_header — a header name/value pair
    //--------------------------------------------------------------------------
    //
    // Matches the shape used by synthetic_http_request / captured_http_response
    // so messages crossing the synthetic edge feed the contract directly.
    //
    using http_header = std::pair<std::string, std::string>;

    //--------------------------------------------------------------------------
    // contract_facet_mode — the binding mode as the interface layer sees it
    //--------------------------------------------------------------------------
    //
    // `validate` contract-checks every request/response crossing the edge;
    // `drive` synthesizes the spec's examples into traffic. `drive` composes on
    // top of `validate`. This is the interface-layer twin of the public
    // `contract_mode` (http_contract.h); the two are kept bit-for-bit identical
    // so a public consumer can name a mode without depending on this header.
    //
    enum class contract_facet_mode : std::uint32_t
    {
        validate = 0,
        drive    = 1,
    };

    static_assert(static_cast<std::uint32_t>(contract_mode::validate) ==
                      static_cast<std::uint32_t>(contract_facet_mode::validate),
                  "contract_mode::validate must match contract_facet_mode::validate");
    static_assert(static_cast<std::uint32_t>(contract_mode::drive) ==
                      static_cast<std::uint32_t>(contract_facet_mode::drive),
                  "contract_mode::drive must match contract_facet_mode::drive");

    //
    // Map the public mode onto the interface mode (used at the facet boundary).
    //
    constexpr contract_facet_mode
    to_facet_mode(contract_mode mode) noexcept
    {
        return static_cast<contract_facet_mode>(static_cast<std::uint32_t>(mode));
    }

    //--------------------------------------------------------------------------
    // synthesized_request — a request produced from the spec's examples (drive)
    //--------------------------------------------------------------------------
    //
    // Drive mode (D-HWC-8) turns a bound spec into traffic: one request per
    // operation, filled from each element's authored `example` (schema-derived
    // default where none is present). `path` carries any query string. The
    // shape matches the synthetic edge so a consumer can submit it directly.
    //
    struct synthesized_request
    {
        std::string               method;
        std::string               path;
        std::vector<http_header>  headers;
        std::vector<std::uint8_t> body;
    };

    //--------------------------------------------------------------------------
    // ihttp_contract_document — a loaded spec that validates messages
    //--------------------------------------------------------------------------
    //
    // Produced by ihttp_contract::load. Holds the normalized model (and, for a
    // live provider, the per-JSON-body-schema validators). Validation is
    // media-type-aware (D-HWC-9): non-JSON bodies get method/path/status,
    // parameter, and declared-header checks but no body-value check.
    //
    struct ihttp_contract_document
    {
        virtual ~ihttp_contract_document() = default;

        //
        //  validate_request
        //
        //  Checks a request against the contract. A conforming request yields a
        //  false disposition and no error. Operational failures (never a
        //  contract violation) flow through `ec`.
        //

        enum class validate_request_result_code : std::uint32_t
        {
            // No operation matched the method + path (+ query discriminator).
            unknown_operation = 1,

            // A path / query / header parameter failed its schema or was absent.
            parameter_invalid = 2,

            // The request body failed its JSON schema (JSON content only).
            body_schema_invalid = 3,
        };

        enum class validate_request_result_flags : std::uint32_t
        {
        };

        using validate_request_disposition =
            disposition<validate_request_result_code, validate_request_result_flags>;

        virtual validate_request_disposition
        validate_request(std::string_view              method,
                         std::string_view              path,
                         std::span<http_header const>  headers,
                         std::span<std::uint8_t const> body,
                         std::error_code&              ec) = 0;

        //
        // Throwing wrapper.
        //
        validate_request_disposition
        validate_request(std::string_view              method,
                         std::string_view              path,
                         std::span<http_header const>  headers,
                         std::span<std::uint8_t const> body)
        {
            std::error_code ec;
            auto const      d = validate_request(method, path, headers, body, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        //  validate_response
        //
        //  Checks a response against the contract for a given request's
        //  method + path. A conforming response yields a false disposition and
        //  no error.
        //

        enum class validate_response_result_code : std::uint32_t
        {
            // No operation matched the method + path (+ query discriminator).
            unknown_operation = 1,

            // The status code is not declared by the operation.
            undeclared_status = 2,

            // The response body failed its JSON schema (JSON content only).
            body_schema_invalid = 3,

            // A response header the spec declares required was absent.
            missing_header = 4,
        };

        enum class validate_response_result_flags : std::uint32_t
        {
        };

        using validate_response_disposition =
            disposition<validate_response_result_code, validate_response_result_flags>;

        virtual validate_response_disposition
        validate_response(std::string_view              method,
                          std::string_view              path,
                          std::uint16_t                 status,
                          std::span<http_header const>  headers,
                          std::span<std::uint8_t const> body,
                          std::error_code&              ec) = 0;

        //
        // Throwing wrapper.
        //
        validate_response_disposition
        validate_response(std::string_view              method,
                          std::string_view              path,
                          std::uint16_t                 status,
                          std::span<http_header const>  headers,
                          std::span<std::uint8_t const> body)
        {
            std::error_code ec;
            auto const      d = validate_response(method, path, status, headers, body, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        //  synthesize_requests
        //
        //  Drive mode (D-HWC-8): produce one request per operation from the
        //  spec's examples (schema-derived fallback), in spec order. The default
        //  yields none (e.g. the null provider); the live document overrides it.
        //
        virtual std::vector<synthesized_request>
        synthesize_requests() const
        {
            return {};
        }
    };

    //--------------------------------------------------------------------------
    // ihttp_contract — the HTTP contract surface
    //--------------------------------------------------------------------------

    struct ihttp_contract
    {
        virtual ~ihttp_contract() = default;

        //
        //  load
        //
        //  Parses a spec's bytes (YAML or JSON) into a validating document. A
        //  provider that resolves `$ref` bundles captures its resolver at
        //  construction (caller-owns-I/O, D-HWC-9); `load` itself takes only
        //  the root bytes. A malformed spec is reported through `ec`.
        //

        enum class load_flags : std::uint64_t
        {
        };

        enum class load_result_code : std::uint32_t
        {
        };

        enum class load_result_flags : std::uint32_t
        {
        };

        using load_disposition = disposition<load_result_code, load_result_flags>;

        virtual load_disposition
        load(load_flags                                flags,
             std::string_view                          spec_bytes,
             std::unique_ptr<ihttp_contract_document>& returned_document,
             std::error_code&                          ec) = 0;

        //
        // Throwing wrapper.
        //
        load_disposition
        load(load_flags                                flags,
             std::string_view                          spec_bytes,
             std::unique_ptr<ihttp_contract_document>& returned_document)
        {
            std::error_code ec;
            auto const      d = load(flags, spec_bytes, returned_document, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: load with default flags.
        //
        std::unique_ptr<ihttp_contract_document>
        load(std::string_view spec_bytes)
        {
            std::unique_ptr<ihttp_contract_document> returned_document;
            load(load_flags{}, spec_bytes, returned_document);
            return returned_document;
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_contract_document::validate_request_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_contract_document::validate_response_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_contract::load_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_contract::load_result_flags);

    //--------------------------------------------------------------------------
    // null_http_contract — the null provider (not implemented)
    //--------------------------------------------------------------------------

    class null_http_contract final : public ihttp_contract
    {
    public:
        null_http_contract()           = default;
        ~null_http_contract() override = default;

        using ihttp_contract::load;

        load_disposition
        load(load_flags,
             std::string_view,
             std::unique_ptr<ihttp_contract_document>&,
             std::error_code& ec) override
        {
            ec = std::make_error_code(std::errc::function_not_supported);
            return {};
        }
    };

    //--------------------------------------------------------------------------
    // Drive mode: submit synthesized traffic and tally response conformance
    //--------------------------------------------------------------------------
    //
    // A response captured back from the engine for a submitted request.
    //
    struct captured_contract_response
    {
        std::uint16_t             status{0};
        std::vector<http_header>  headers;
        std::vector<std::uint8_t> body;
    };

    //
    // The engine that turns a synthesized request into a captured response. On
    // the synthetic edge this enqueues the request and waits for the response;
    // in tests it is a fake engine. The contract layer stays engine-agnostic by
    // submitting through this caller-supplied callable (Design Autonomy).
    //
    using engine_submit = std::function<captured_contract_response(synthesized_request const&)>;

    //
    // Outcome of a drive run.
    //
    struct drive_tally
    {
        std::size_t requests{0};            // requests synthesized + submitted
        std::size_t responses_validated{0}; // responses run through validate_response
        std::size_t conforming{0};          // validated responses that conformed
        std::size_t violating{0};           // validated responses that violated the contract
    };

    //
    // Drive a loaded document (D-HWC-8): synthesize one request per operation
    // (document.synthesize_requests()), submit each through `submit`, and run the
    // captured response through the same document's validate_response, tallying
    // conforming vs violating. Operational failures (a non-contract error from
    // validate_response) are not counted as violations. Returns the run tally.
    //
    drive_tally
    drive_contract(ihttp_contract_document& document, engine_submit const& submit);

} // namespace m::pil
