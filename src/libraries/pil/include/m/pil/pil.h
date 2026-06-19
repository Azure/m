// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <filesystem>
#include <span>
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
    make_platform(
        make_platform_flags                                              flags = make_platform_flags{},
        std::span<std::pair<std::u16string_view, std::u16string_view> const> redirections = {});

    //
    // Interface-level factory: returns the underlying iplatform stack directly
    // rather than the value-wrapper `platform`. Use this when you need to drive
    // the raw interfaces (iplatform / iregistry / ikey) — for example, a Win32
    // shim that resolves predefined HKEYs to their backing ikey. The value
    // wrappers cannot surface the raw ikey, so consumers operating at the
    // interface layer must obtain the iplatform from here.
    //
    std::shared_ptr<iplatform>
    make_platform_interface(
        make_platform_flags                                                 flags = make_platform_flags{},
        std::span<std::pair<std::u16string_view, std::u16string_view> const> redirections = {});

    //
    // Snapshot factories: build a platform from a previously persisted state
    // file. The returned platform has no underlying (live) platform, so reads
    // and writes operate purely against the loaded snapshot and never touch the
    // running system (mode (c)).
    //
    platform
    load_platform(std::filesystem::path const& persisted_state);

    std::shared_ptr<iplatform>
    load_platform_interface(std::filesystem::path const& persisted_state);
} // namespace m::pil
