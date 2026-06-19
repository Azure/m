// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>

namespace m::pil::impl
{
    enum class create_platform_interface_flags : uint32_t
    {
        /// <summary>
        /// Set the record_modifications flag to enable logging of changes to the platform
        /// that can be written out at any time.
        /// </summary>
        record_modifications = 1 << 0,

        /// <summary>
        /// Set the buffer_updates flag to buffer the updates away from being applied to
        /// the live system.
        /// </summary>
        buffer_updates = 1 << 1,
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(create_platform_interface_flags);

    std::shared_ptr<iplatform>
    create_platform_interface(
        create_platform_interface_flags flags = create_platform_interface_flags{},
        std::span<std::pair<std::u16string_view, std::u16string_view> const> redirections = {});
} // namespace m::pil::impl
