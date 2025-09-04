// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/win32/registry.h>

#include "pcwstr.h"
#include "win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::win32
{
    registry::registry(std::shared_ptr<m::work_queue> wq): m_work_queue(std::move(wq)) {}

    iregistry::open_predefined_key_disposition
    registry::open_predefined_key(open_predefined_key_flags flags,
                                  predefined_key            pk,
                                  sam                       sam_desired,
                                  std::shared_ptr<ikey>&    returned_key)
    {
        returned_key.reset();

        M_VALIDATE_FLAGS_PARAMETER(flags, open_predefined_key_flags{});

        auto key_name = m::pil::map_predefined_key_to_string(pk);

        m::win32::registry::hkey
            hk{}; // all of these are pseudo-keys but use the managed type for tidiness

        switch (pk)
        {
            using enum predefined_key;

            case classes_root: hk.reset(HKEY_CLASSES_ROOT); break;
            case current_user: hk.reset(HKEY_CURRENT_USER); break;
            case local_machine: hk.reset(HKEY_LOCAL_MACHINE); break;
            case users: hk.reset(HKEY_USERS); break;
            case performance_data: hk.reset(HKEY_PERFORMANCE_DATA); break;
            case current_config: hk.reset(HKEY_CURRENT_CONFIG); break;
            case current_user_local_settings: hk.reset(HKEY_CURRENT_USER_LOCAL_SETTINGS); break;
            case performance_text: hk.reset(HKEY_PERFORMANCE_TEXT); break;
            case performance_nlstext: hk.reset(HKEY_PERFORMANCE_NLSTEXT); break;

            default: throw std::runtime_error("invalid predefined key value passed");
        }

        m::win32::registry::hkey hk2{};

        auto status =
            ::RegOpenKeyExW(hk, nullptr, 0, static_cast<REGSAM>(sam_desired), hk2.addressof());
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        returned_key = std::make_shared<pil::impl::win32::key>(
            std::move(hk2), m::pil::key_path(pk));

        return open_predefined_key_disposition{};
    }

    iregistry::monitor_disposition
    registry::monitor(monitor_flags                               flags,
                      std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor)
    {
        if (flags != monitor_flags{})
            throw std::runtime_error("Invalid flags to call to iregistry::monitor()");

        auto lock = std::unique_lock(m_mutex);

        if (!m_monitor)
            initialize_monitor(m::locked);

        M_INTERNAL_ERROR_CHECK(m_monitor);

        returned_registry_monitor = m_monitor;
        return monitor_disposition{};
    }

    void
    registry::initialize_monitor(m::locked_t)
    {
        if (m_monitor)
            return;

        m_monitor = std::make_shared<registry_monitor>(m_work_queue);
    }

} // namespace m::pil::impl::win32
