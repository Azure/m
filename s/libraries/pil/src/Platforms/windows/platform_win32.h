// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>

#include <m/pil/pil.h>
// #include <m/pil/platform.h>

#include <m/pil/platform_interfaces.h>

namespace m::pil::impl::platform::win32
{
    class direct_platform : public iplatform, public std::enable_shared_from_this<direct_platform>
    {
    public:
        direct_platform() = default;
        direct_platform(direct_platform&& other) noexcept;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;
    };
} // namespace m::pil
