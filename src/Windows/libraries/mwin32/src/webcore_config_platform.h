// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <m/pil/http_contract_interfaces.h>
#include <m/pil/platform_interfaces.h>

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
    // Create a platform that wraps the underlying platform and applies the
    // webcore configuration when get_webcore() is called.
    //
    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const& underlying_platform,
                         pilcfg::webcore_config const&             webcore_cfg);
} // namespace m::mwin32_impl
