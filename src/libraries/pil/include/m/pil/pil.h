// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/pil/platform.h>

namespace m::pil
{
    enum class make_platform_flags : uint32_t
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

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(make_platform_flags);

    platform
    make_platform(make_platform_flags flags = make_platform_flags{},
                  std::initializer_list<std::pair<std::u16string_view, std::u16string_view>>*
                      redirections = nullptr);
} // namespace m::pil
