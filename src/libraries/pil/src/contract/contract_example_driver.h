// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <m/pil/http_contract_interfaces.h>

#include "openapi_model.h"

//
// Drive mode for the HWC HTTP contract surface (M-HWC-CONTRACT-DRIVE, D-HWC-8).
//
// Drive mode turns a bound spec into *traffic*: for each operation it
// synthesizes a request from the operation's authored `example` / `examples`
// (parameters and request body), falling back to schema-derived defaults where
// no example is present. The synthesized requests are then submitted to an
// engine; when a validate document is also bound, each captured response is run
// through `validate_response` and a conforming / violating tally is reported.
// Drive composes on top of validate (D-HWC-8).
//
// Layering: the synthesizer and the driver are pure over the model and engine-
// agnostic — `drive_contract` submits each request through a caller-supplied
// `engine_submit` callable. On Windows that callable enqueues into the
// `synthetic_http_queue` and waits for the captured response; in tests it is a
// fake engine. This keeps the contract layer cross-platform and free of any
// dependency on the Windows-only synthetic edge (Design Autonomy).
//
// This header is internal to m_pil (lives under src/, not include/).
//

namespace m::pil
{
    // `synthesized_request`, `captured_contract_response`, `engine_submit`, and
    // `drive_tally` are the public drive surface (see http_contract_interfaces.h,
    // EXPOSE-2). This internal header adds the model-level synthesizer and the
    // lower-level driver that the live document and the public `drive_contract`
    // are built on.

    //
    // Synthesize one request per operation in the model (DRIVE-1). Pure over the
    // model: path-template captures, required query parameters (including query
    // discriminators), declared header parameters, and the JSON request body are
    // filled from each element's `example`, falling back to a schema-derived
    // default when no example is present. Operations are emitted in model order.
    //
    std::vector<synthesized_request>
    synthesize_contract_requests(openapi_model const& model);

    //
    // Drive the synthesized requests through `submit` (DRIVE-2). When
    // `validator` is non-null, each captured response is run through
    // `validate_response` (keyed by the request's method + path) and tallied as
    // conforming or violating; when null, requests are still submitted but no
    // response is validated. Returns the run tally.
    //
    drive_tally
    drive_contract(std::vector<synthesized_request> const& requests,
                   engine_submit const&                    submit,
                   ihttp_contract_document*                validator);
}
