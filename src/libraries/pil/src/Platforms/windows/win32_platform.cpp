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
#include "win32_platform.h"
#include "win32_registry.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::platform::win32
{
    platform::platform(std::shared_ptr<m::work_queue> wq): m_work_queue(std::move(wq)) {}

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_registry_flags{});
        auto l            = std::unique_lock(m_mutex);
        auto newreg       = std::make_shared<m::pil::impl::registry::win32::registry>(m_work_queue);
        returned_registry = newreg;
        return get_registry_disposition{};
    }
} // namespace m::pil::impl::platform::win32
