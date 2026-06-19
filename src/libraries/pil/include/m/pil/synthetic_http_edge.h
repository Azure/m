// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <functional>

#include <m/pil/http_contract_interfaces.h>

//
// Public synthetic-HTTP edge seam (D-HWC-11). An `isynthetic_http_edge` is the
// single object a consumer's traffic crosses into an activated engine's
// in-process HTTP edge (the synthetic queue behind the engine, D-HWC-6 Tier B):
//
//   - `submit(request, timeout)` is the drive-injection path: it pushes a
//     synthesized request into the engine and returns the captured response.
//     On the real edge this enqueues onto the synthetic queue and waits for the
//     engine to service it; in the in-process engine a worker thread services it.
//
//   - `add_crossing_observer(observer)` registers a per-crossing tap: the
//     observer is invoked with every completed (request, response) pair crossing
//     the edge — both drive-injected traffic and autonomous client traffic the
//     engine services. This is how validate mode auto-checks live traffic (D6:
//     a side diagnostic that never alters the engine's behavior).
//
// The seam names only the public contract message types (`synthesized_request`
// / `captured_contract_response`) — never the Win32 `synthetic_http_queue` /
// `<http.h>` — so it stays cross-platform-clean (Design Autonomy). An activated
// `iwebcore_instance` exposes its edge (if any) via
// `iwebcore_instance::synthetic_http_edge()`; an instance with no in-process
// edge (e.g. the null engine) returns `nullptr`.
//

namespace m::pil
{
    //
    // A per-crossing tap (D-HWC-11, D6). Invoked with the request that crossed
    // the edge paired with the response the engine produced for it. Observers
    // run after the response completes and must not throw; they are a side
    // diagnostic and never alter the engine's behavior.
    //
    using crossing_observer =
        std::function<void(synthesized_request const&, captured_contract_response const&)>;

    //
    // The in-process synthetic HTTP edge of an activated engine. See the file
    // header for the model. A consumer obtains one from
    // `iwebcore_instance::synthetic_http_edge()`.
    //
    struct isynthetic_http_edge
    {
        virtual ~isynthetic_http_edge() = default;

        //
        // Submit one synthesized request into the engine and block (up to
        // `timeout`) for the captured response. A timeout yields a default
        // (status 0) `captured_contract_response`.
        //
        virtual captured_contract_response
        submit(synthesized_request const& request, std::chrono::milliseconds timeout) = 0;

        //
        // Register a per-crossing observer. The observer is invoked for every
        // request/response pair that crosses the edge from registration onward.
        //
        virtual void
        add_crossing_observer(crossing_observer observer) = 0;
    };

    //
    // Adapt an edge to the `engine_submit` callable `drive_contract` consumes:
    // each submitted request crosses `edge` with the bound `timeout`. The
    // returned callable holds a reference to `edge`; `edge` must outlive it.
    //
    inline engine_submit
    make_engine_submit(isynthetic_http_edge& edge, std::chrono::milliseconds timeout)
    {
        return [&edge, timeout](synthesized_request const& request) {
            return edge.submit(request, timeout);
        };
    }

    //
    // Convenience over the pointer returned by
    // `iwebcore_instance::synthetic_http_edge()`: a null edge (no in-process
    // edge on the activation) yields a null `engine_submit`.
    //
    inline engine_submit
    make_engine_submit(isynthetic_http_edge* edge, std::chrono::milliseconds timeout)
    {
        if (edge == nullptr)
            return engine_submit{};
        return make_engine_submit(*edge, timeout);
    }
} // namespace m::pil
