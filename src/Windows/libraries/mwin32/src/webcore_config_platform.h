// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    // Create a platform that wraps the underlying platform and applies the
    // webcore configuration when get_webcore() is called.
    //
    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const& underlying_platform,
                         pilcfg::webcore_config const&             webcore_cfg);
} // namespace m::mwin32_impl
