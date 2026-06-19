// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <memory>

#include <m/pil/http_contract_interfaces.h>

//
// Public contract-edge seam (D-HWC-10). An `ihttp_contract_edge` is the single
// point a consumer's HTTP traffic crosses into the engine. It ties any number
// of bound contracts to one engine: documents attached in `validate` mode
// auto-check every request and response crossing the edge (tracing violations
// as a side diagnostic per D6 — the engine's behavior is never altered), while
// `drive`-mode documents are submitted through the same seam.
//
// The engine is pluggable (Design Autonomy): `make_contract_edge` takes an
// `engine_submit` callable — in production it bridges to the activated engine's
// Windows synthetic queue (still the consumer's job, D-HWC-8); in tests it is a
// fake engine. The edge itself names only the public contract message types
// (`synthesized_request` / `captured_contract_response`) and never the Windows
// synthetic edge, so it stays cross-platform-clean.
//

namespace m::pil
{
    //--------------------------------------------------------------------------
    // contract_edge_tally — lifetime totals observed by a contract edge
    //--------------------------------------------------------------------------
    //
    // `request_violations` / `response_violations` count per attached document:
    // one crossing checked by two attached documents can contribute two.
    //
    struct contract_edge_tally
    {
        std::size_t requests{0};            // submit() calls
        std::size_t responses{0};           // engine responses returned by submit()
        std::size_t request_violations{0};  // request checks that flagged a violation
        std::size_t response_violations{0}; // response checks that flagged a violation
    };

    //--------------------------------------------------------------------------
    // ihttp_contract_edge — the contract-checked HTTP edge seam
    //--------------------------------------------------------------------------

    struct ihttp_contract_edge
    {
        virtual ~ihttp_contract_edge() = default;

        //
        //  submit
        //
        //  Push a request through the edge: validate it against every attached
        //  validate-mode document, call the engine, validate the engine's
        //  response against every attached document, update the tally, and
        //  return the engine's response. A contract violation is traced and
        //  counted; it never alters the response the engine produced (D6).
        //
        virtual captured_contract_response
        submit(synthesized_request const& request) = 0;

        //
        //  attach_validation
        //
        //  Register a document whose validate_request / validate_response runs on
        //  every crossing (validate mode). Multiple documents may be attached;
        //  each is checked on every crossing. The edge shares ownership for its
        //  lifetime. A document driven via as_engine_submit() should not also be
        //  attached, or its crossings would be counted twice.
        //
        virtual void
        attach_validation(std::shared_ptr<ihttp_contract_document> document) = 0;

        //
        //  tally
        //
        //  Lifetime totals across every submit() so far.
        //
        virtual contract_edge_tally
        tally() const = 0;

        //
        //  as_engine_submit
        //
        //  Adapt this edge to the engine_submit callable drive_contract expects:
        //  the returned callable forwards to submit(), so drive traffic crosses
        //  (and is validated by) the same edge. The callable holds a raw pointer
        //  to this edge and must not outlive it.
        //
        engine_submit
        as_engine_submit()
        {
            return [this](synthesized_request const& request) { return submit(request); };
        }
    };

    //--------------------------------------------------------------------------
    // make_contract_edge — create an in-process contract edge over an engine
    //--------------------------------------------------------------------------
    //
    // The edge validates each crossing against attached documents and tallies the
    // result; `engine` is the seam to the real or fake engine behind it.
    //
    std::shared_ptr<ihttp_contract_edge>
    make_contract_edge(engine_submit engine);

} // namespace m::pil
