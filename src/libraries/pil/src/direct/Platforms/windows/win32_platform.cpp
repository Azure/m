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

#include "pcwstr.h"
#include "win32.h"
#include "win32_security_attributes.h"
#include "win32_webcore.h"

namespace m::pil::impl::win32
{
    platform::platform(std::shared_ptr<m::work_queue> wq): m_work_queue(std::move(wq)) {}

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_registry_flags{});
        auto l            = std::unique_lock(m_mutex);
        auto newreg       = std::make_shared<m::pil::impl::win32::registry>(m_work_queue);
        returned_registry = newreg;
        return get_registry_disposition{};
    }

    iplatform::get_filesystem_disposition
    platform::get_filesystem(get_filesystem_flags          flags,
                             std::shared_ptr<ifilesystem>& returned_filesystem)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_filesystem_flags{});
        auto l              = std::unique_lock(m_mutex);
        auto newfs          = std::make_shared<m::pil::impl::win32::filesystem>(m_work_queue);
        returned_filesystem = newfs;
        return get_filesystem_disposition{};
    }

    iplatform::get_webcore_disposition
    platform::get_webcore(get_webcore_flags          flags,
                          std::shared_ptr<iwebcore>& returned_webcore)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_webcore_flags{});
        auto l           = std::unique_lock(m_mutex);
        auto newwc       = std::make_shared<m::pil::impl::win32::webcore>();
        returned_webcore = newwc;
        return get_webcore_disposition{};
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents, pugi::xml_node&)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // Nothing to save!
        return save_disposition{};
    }

} // namespace m::pil::impl::win32
