// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>

namespace m::pil::impl
{
    std::shared_ptr<iplatform>
    create_platform_interface(platform_type pt);
}
