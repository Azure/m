// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "pcwstr.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::registry::win32
{

    iregistry::open_predefined_key_disposition
    registry::open_predefined_key(open_predefined_key_flags flags,
                                  predefined_key            pk,
                                  sam                       sam_desired,
                                  std::shared_ptr<ikey>&    returned_key)
    {
        returned_key.reset();

        if (flags != open_predefined_key_flags{})
            throw std::runtime_error("Invalid flags to iregistry::open_predefined_key() call");

        hkey hk{}; // all of these are pseudo-keys but use the managed type for tidiness

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

        hkey hk2{};

        auto status = ::RegOpenKeyExW(hk, nullptr, 0, static_cast<REGSAM>(sam_desired), hk2.ptr());
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        returned_key = std::make_shared<pil::impl::registry::win32::key>(std::move(hk2));

        return open_predefined_key_disposition{};
    }

} // namespace m::pil::impl::registry::win32
