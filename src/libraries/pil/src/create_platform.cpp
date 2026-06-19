// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

#include "platform.h"

#ifdef WIN32
#include <m/errors/errors.h>
#include <m/threadpool/threadpool.h>
#endif

#include "buffered/buffered.h"
#include "logging/logging.h"
#include "redirecting/redirecting.h"

#ifdef WIN32
#include "win32.h"
#include "win32_security_attributes.h"
#endif

namespace m::pil::impl
{
    std::shared_ptr<iplatform>
    create_platform_interface(
        create_platform_interface_flags                                     flags,
        std::span<std::pair<std::u16string_view, std::u16string_view> const> redirections)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags,
                                   create_platform_interface_flags::record_modifications |
                                       create_platform_interface_flags::buffer_updates);

#ifdef WIN32
        auto wq = m::threadpool->create_work_queue(m::work_queue_execution_policy::parallel);

        auto                       plat = std::make_shared<win32::platform>(wq);
        std::shared_ptr<iplatform> top  = plat;

        // If buffering is requested, put a buffering layer in between
        if (!!(flags & create_platform_interface_flags::buffer_updates))
            top = std::make_shared<m::pil::impl::buffered::platform>(top);

        if (!redirections.empty())
            // The public factory exposes a single redirection table, which applies
            // to the whole platform surface: the same prefix map is installed for
            // both the registry and the filesystem. Each surface only matches paths
            // of its own shape, so a registry-shaped rule is inert against
            // filesystem paths and vice versa. (Passing the table only as the
            // registry argument here would silently drop filesystem redirection,
            // since the filesystem table defaults to empty.)
            top = std::make_shared<redirecting::platform>(top, redirections, redirections);

        if (!!(flags & create_platform_interface_flags::record_modifications))
            top = std::make_shared<logging::platform>(top);

        return top;
#else
        std::ignore = redirections;
        M_NOT_IMPLEMENTED("platform creation not implemented yet for Linux");
#endif
    }
} // namespace m::pil::impl
