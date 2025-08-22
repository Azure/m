// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "buffered/buffered_platform.h"
#include "pcwstr.h"
#include "win32_platform.h"
#include "win32_registry.h"
#include "win32_security_attributes.h"

namespace m::pil::impl
{
    std::shared_ptr<iplatform>
    create_platform_interface(platform_type pt)
    {
        //
        switch (pt)
        {
            using enum platform_type;

            case direct:
            {
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                return std::make_shared<platform::win32::platform>(wq);
            }

            case buffered_over_direct:
            {
                auto wq =
                    m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);
                return std::make_shared<m::pil::impl::buffered::platform>(
                    std::make_shared<platform::win32::platform>(wq));
            }
        }

        throw std::runtime_error("Unhandled platform_type value passed to make_platform()");
    }
} // namespace m::pil::impl
