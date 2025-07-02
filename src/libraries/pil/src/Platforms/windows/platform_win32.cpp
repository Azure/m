// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "buffered.h"
#include "direct_platform.h"
#include "pcwstr.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl
{
    std::shared_ptr<iplatform>
    create_platform_interface(platform_type pt)
    {
        //
        switch (pt)
        {
            using enum platform_type;

            case direct: return std::make_shared<platform::win32::direct_platform>();

            case buffered_over_direct:
                return std::make_shared<m::pil::impl::buffered::platform>(
                    std::make_shared<platform::win32::direct_platform>());
        }

        throw std::runtime_error("Unhandled platform_type value passed to make_platform()");
    }
} // namespace m::pil::impl
