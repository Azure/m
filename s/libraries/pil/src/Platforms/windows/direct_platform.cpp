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

#include "pcwstr.h"
#include "platform_win32.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::platform::win32
{
    iplatform::get_registry_disposition
    direct_platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("Invalid flags to iplatform::get_registry() call");

        returned_registry = std::make_shared<m::pil::impl::registry::win32::registry>();

        return get_registry_disposition{};
    }
} // namespace m::pil::impl::registry::win32
