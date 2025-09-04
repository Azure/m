// Copyright (c) Microsoft Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>

#include <m/pil/pil.h>
// #include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>
#include <m/threadpool/threadpool.h>

namespace m::pil::impl::platform::win32
{
    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;

        platform(std::shared_ptr<m::work_queue> wq);

        platform(platform const&)           = delete;
        platform(platform&& other) noexcept = delete;
        ~platform()                         = default;

        platform&
        operator=(platform const&) = delete;

        platform&
        operator=(platform&& other) noexcept = delete;

        void
        swap(platform& other) noexcept = delete;

        // Implementation of iplatform:

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

    private:
        std::mutex                     m_mutex;
        std::shared_ptr<m::work_queue> m_work_queue;
    };

} // namespace m::pil::impl::platform::win32
