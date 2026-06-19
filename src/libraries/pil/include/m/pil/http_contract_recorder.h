// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <m/pil/http_contract_interfaces.h>

//
// Public façade for the HTTP contract recorder (REC-4): derive an OpenAPI
// contract from observed request/response crossings — the "derive the
// contracts" path of the wire-capture lifecycle demo, and the inverse of
// `ihttp_contract::load`.
//
// A consumer (e.g. the mwin32 wire capture in record mode) feeds every clean
// crossing it reassembles to `observe_request` / `observe_response`, then calls
// `emit_spec` at shutdown to obtain a loadable OpenAPI 3.0 YAML document. That
// document can be loaded back through `make_http_contract_provider()` (the live
// `ihttp_contract`) to drive validate mode on a later run.
//
// The recorder names the same `http_header` vocabulary as the validate surface
// (http_contract_interfaces.h) so a reassembled crossing feeds the recorder and
// the validator without an adapter. Behavior is owned by PIL (Design Autonomy):
// operations are correlated by HTTP method + clean path (query string stripped;
// observed path used verbatim as the path template), JSON bodies are reduced to
// a permissive structural schema (field names, value types, array element
// shape — not values), and declared response headers are the intersection of
// headers seen on every response of a status, minus volatile transport headers.
//

namespace m::pil
{
    //--------------------------------------------------------------------------
    // ihttp_contract_recorder — accumulate crossings, emit an OpenAPI spec
    //--------------------------------------------------------------------------

    struct ihttp_contract_recorder
    {
        virtual ~ihttp_contract_recorder() = default;

        //
        //  observe_request
        //
        //  Record a request crossing. `body` is the raw (already-decoded) body
        //  bytes; it is parsed as JSON only when the Content-Type is JSON (or
        //  absent and the bytes parse as JSON). Re-observing the same crossing
        //  is idempotent in shape.
        //
        virtual void
        observe_request(std::string_view              method,
                        std::string_view              path,
                        std::span<http_header const>  headers,
                        std::span<std::uint8_t const> body) = 0;

        //
        //  observe_response
        //
        //  Record a response crossing for the given request's method + path.
        //
        virtual void
        observe_response(std::string_view              method,
                         std::string_view              path,
                         std::uint16_t                 status,
                         std::span<http_header const>  headers,
                         std::span<std::uint8_t const> body) = 0;

        //
        //  emit_spec
        //
        //  Serialize everything observed so far to an OpenAPI 3.0 YAML document
        //  loadable by `make_http_contract_provider()`'s `ihttp_contract::load`.
        //
        virtual std::string
        emit_spec() const = 0;

        //
        //  operation_count
        //
        //  Number of distinct (method, path) operations observed so far.
        //
        virtual std::size_t
        operation_count() const = 0;
    };

    //
    // Build a contract recorder. The returned recorder owns no I/O; the consumer
    // feeds it crossings and decides when to `emit_spec`.
    //
    std::unique_ptr<ihttp_contract_recorder>
    make_http_contract_recorder();

} // namespace m::pil
