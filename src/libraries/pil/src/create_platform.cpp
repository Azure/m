// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/pil/common.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

#ifdef WIN32
#include <m/errors/errors.h>
#include <m/threadpool/threadpool.h>
#endif

#include "buffered/buffered.h"
#include "redirecting/redirecting.h"

#ifdef WIN32
#include "win32.h"
#include "win32_security_attributes.h"
#endif

namespace m::pil::impl
{
    // std::initializer_list<std::pair<std::u16string_view, std::u16string_view>>

    std::shared_ptr<iplatform>
    create_platform_interface(platform_type pt)
    {
        //
        switch (pt)
        {
            using enum platform_type;

            case direct:
            {
#ifdef WIN32
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                return std::make_shared<win32::platform>(wq);
#else
                M_NOT_IMPLEMENTED("platform_type::direct not implemented yet for Linux");
#endif
            }

            case buffered_over_direct:
            {
#ifdef WIN32
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                return std::make_shared<m::pil::impl::buffered::platform>(
                    std::make_shared<win32::platform>(wq));
#else
                M_NOT_IMPLEMENTED(
                    "platform_type::buffered_over_direct not implemented yet for Linux");
#endif
            }

            default: M_UNREACHABLE_CODE();
        }
    }

    std::shared_ptr<iplatform>
    create_platform_interface(
        platform_type pt,
        std::initializer_list<std::pair<std::u16string_view, std::u16string_view>> redirections)
    {
        //
        switch (pt)
        {
            using enum platform_type;

            case direct:
            {
#ifdef WIN32
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                auto plat = std::make_shared<win32::platform>(wq);
                return std::make_shared<redirecting::platform>(plat, redirections);
#else
                std::ignore = redirections;
                M_NOT_IMPLEMENTED("platform_type::direct not implemented yet for Linux");
#endif
            }

            case buffered_over_direct:
            {
#ifdef WIN32
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                return std::make_shared<m::pil::impl::buffered::platform>(
                    std::make_shared<win32::platform>(wq));
#else
                M_NOT_IMPLEMENTED(
                    "platform_type::buffered_over_direct not implemented yet for Linux");
#endif
            }

            default: M_UNREACHABLE_CODE();
        }
    }
} // namespace m::pil::impl
