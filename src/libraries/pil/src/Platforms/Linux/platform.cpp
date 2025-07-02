// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/pil/common.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/utility/utility.h>

//
//

namespace m::pil::impl
{
    std::shared_ptr<iplatform>
    create_platform_interface(platform_type pt)
    {
        //
        switch (pt)
        {
            using enum platform_type;

            case direct: M_NOT_IMPLEMENTED("platform_type::direct");

            case buffered_over_direct: M_NOT_IMPLEMENTED("platform_type::buffered_over_direct");
        }

        throw std::runtime_error("Unhandled platform_type value passed to make_platform()");
    }
} // namespace m::pil::impl
