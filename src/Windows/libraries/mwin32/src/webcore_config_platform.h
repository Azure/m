// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <m/pil/http_contract_edge.h>
#include <m/pil/http_contract_interfaces.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/webcore_interfaces.h>

#include "pilcfg.h"

//
// Webcore-configuring platform decorator (M-HWC-SHIM-4).
//
// This platform wrapper stores the webcore configuration from pilcfg and
// intercepts get_webcore() to apply the configuration. All other operations
// are forwarded to the underlying platform.
//
// The webcore configuration includes:
//   - interception mode (Detours vs. materialization)
//   - endpoints table (URL namespace mapping)
//   - materialization_dir (for materialized configs)
//   - fault_script (webcore-specific fault injection)
//
// This is analogous to how apply_fault_layer wraps the platform to inject
// faults; here we wrap to configure the webcore surface.
//

namespace m::mwin32_impl
{
    //
    // A contract spec loaded from a `.pilcfg` `webcore.contracts` entry and bound
    // to its endpoint + mode (M-HWC-CONTRACTCFG-3, PIL D-HWC-8). The document is
    // the PIL validating surface: it validates requests/responses and (for drive)
    // synthesizes example traffic. `endpoint` is the literal logical key the
    // binding names; `mode` selects validate vs. drive.
    //
    struct bound_contract
    {
        std::u16string                                    endpoint;
        pilcfg::webcore_config::contract_mode             mode;
        std::shared_ptr<m::pil::ihttp_contract_document>  document;
    };

    //
    // Load and bind every `webcore.contracts` entry against `platform`'s contract
    // provider (M-HWC-CONTRACTCFG-3). For each entry the spec bytes are read from
    // the (already `%VAR%`-expanded) host path and loaded into a validating
    // document. Loading is best-effort and tolerant (per D5/D7): a missing or
    // malformed spec is skipped (the binding is simply absent) rather than
    // breaking the host. Successfully bound contracts are returned in config
    // order.
    //
    std::vector<bound_contract>
    load_webcore_contracts(m::pil::iplatform&            platform,
                           pilcfg::webcore_config const& webcore_cfg);

    //
    // Aggregate outcome of wiring bound contracts onto a contract edge
    // (M-HWC-CONTRACTCFG-6, PIL D-HWC-10). `validate_bindings` / `drive_bindings`
    // count how many of each mode were wired; `drive` sums every drive binding's
    // tally. The edge's own tally (`edge.tally()`) reports the validate-mode
    // crossings observed by the attached documents.
    //
    struct contract_wiring_summary
    {
        std::size_t         validate_bindings{0};
        std::size_t         drive_bindings{0};
        m::pil::drive_tally drive;
    };

    //
    // Wire the bound contracts onto `edge` (M-HWC-CONTRACTCFG-6, PIL D-HWC-10):
    // attach every `validate`-mode document so it auto-checks each request and
    // response crossing the edge, and submit every `drive`-mode document through
    // the edge (`drive_contract`), summing their tallies. A binding whose document
    // failed to load (null) is skipped. Returns the aggregate summary; the edge's
    // validate-mode crossings are read separately from `edge.tally()`.
    //
    contract_wiring_summary
    wire_contracts_to_edge(std::vector<bound_contract> const& contracts,
                           m::pil::ihttp_contract_edge&       edge);

    //
    // Live wiring of bound contracts onto a *running* engine's synthetic HTTP
    // edge (M-HWC-CONTRACTCFG-7.1, PIL D-HWC-11). Unlike `wire_contracts_to_edge`
    // (which drives a synchronous `ihttp_contract_edge`), this wires onto the
    // activated engine instance's `isynthetic_http_edge`: `validate`-mode
    // documents become crossing observers that auto-check autonomous traffic, and
    // `drive`-mode documents are driven against the activated engine.
    //
    // Accumulated live-wiring diagnostics (a side diagnostic, D6 — never altering
    // the engine). `validate_crossings` / `validate_violations` count what the
    // registered `validate`-mode observers saw across every crossing (including
    // drive traffic); `drive` sums each `drive`-mode binding's tally captured at
    // activation. All fields are read/written under `mutex`, since the validate
    // observers run on the engine's servicing thread.
    //
    struct live_contract_diagnostics
    {
        std::mutex          mutex;
        std::size_t         validate_bindings{0};
        std::size_t         drive_bindings{0};
        m::pil::drive_tally drive;
        std::size_t         validate_crossings{0};
        std::size_t         validate_violations{0};
    };

    //
    // How long a `drive`-mode binding waits for each submitted request's response
    // when driven against the activated engine (M-HWC-CONTRACTCFG-7.1). The
    // in-process and intercepting engines service synthetic requests promptly;
    // this is a generous upper bound, not an expected latency.
    //
    inline constexpr std::chrono::milliseconds default_contract_drive_timeout{5000};

    //
    // Wrap an engine with the contract-wiring decorator (M-HWC-CONTRACTCFG-7.1).
    // The returned `iwebcore` forwards `activate` / `set_metadata` to `underlying`;
    // each activation additionally wires `contracts` onto the activated instance's
    // `synthetic_http_edge()` — registering `validate`-mode observers and driving
    // `drive`-mode documents — and the returned instance owns that wiring for its
    // lifetime. If an activated instance exposes no synthetic edge
    // (`synthetic_http_edge() == nullptr`, e.g. the null engine), wiring is a
    // tolerant no-op. Observations accumulate into `diagnostics` (shared so a
    // caller can read them, D6).
    //
    std::shared_ptr<m::pil::iwebcore>
    make_contract_wiring_webcore(std::shared_ptr<m::pil::iwebcore>          underlying,
                                 std::vector<bound_contract>                contracts,
                                 std::shared_ptr<live_contract_diagnostics> diagnostics,
                                 std::chrono::milliseconds                  drive_timeout =
                                     default_contract_drive_timeout);

    //
    // Create a platform that wraps the underlying platform and applies the
    // webcore configuration when get_webcore() is called.
    //
    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const& underlying_platform,
                         pilcfg::webcore_config const&             webcore_cfg);

    //
    // Diagnostic overload (M-HWC-CONTRACTCFG-7.2): identical to the two-argument
    // form, but the contract-wiring decorator's live diagnostics accumulate into
    // the caller-supplied `diagnostics`, so a test can confirm the validate
    // observers ran and the drive contracts executed against the activated engine.
    //
    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const&   underlying_platform,
                         pilcfg::webcore_config const&               webcore_cfg,
                         std::shared_ptr<live_contract_diagnostics>  diagnostics);
} // namespace m::mwin32_impl
