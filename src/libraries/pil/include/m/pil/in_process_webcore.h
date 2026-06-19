// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <functional>
#include <memory>

#include <m/pil/http_contract_interfaces.h>
#include <m/pil/webcore_interfaces.h>

//
// In-process webcore engine (D-HWC-11). `make_in_process_webcore` builds an
// `iwebcore` whose activation runs a real, in-process HTTP edge serviced by a
// caller-supplied handler — the deterministic, IIS-free engine the synthetic
// edge (D-HWC-6 Tier B) was designed for. The activated instance exposes
// `iwebcore_instance::synthetic_http_edge()` (see m/pil/synthetic_http_edge.h),
// so a consumer drives traffic and taps crossings exactly as it would against a
// real engine; only the config-selected underlying engine differs.
//
// The handler is the test/host's stand-in for application logic: it maps each
// synthesized request to the response the engine should produce (conforming or
// not). It is invoked on a worker thread the instance owns and joins on
// destruction.
//
// This engine is realized on Windows (it reuses the intercepting synthetic
// queue); the declaration speaks only public contract types.
//

namespace m::pil
{
    //
    // Maps a synthesized request to the response the in-process engine produces
    // for it. Invoked on the engine's worker thread.
    //
    using synthetic_request_handler =
        std::function<captured_contract_response(synthesized_request const&)>;

    //
    // Build an in-process `iwebcore` engine driven by `handler`. Activating it
    // yields an instance whose `synthetic_http_edge()` is non-null.
    //
    std::shared_ptr<iwebcore>
    make_in_process_webcore(synthetic_request_handler handler);
} // namespace m::pil
