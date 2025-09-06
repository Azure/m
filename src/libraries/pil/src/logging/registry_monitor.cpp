// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "logging.h"

namespace m::pil::impl::logging
{
    registry_monitor::registry_monitor(
        std::shared_ptr<iregistry_monitor> const& underlying_registry_monitor):
        m_underlying_registry_monitor(underlying_registry_monitor)
    {}

    iregistry_monitor::register_watch_disposition
    registry_monitor::register_watch(
        register_watch_flags                                flags,
        pil::key_path const&                                path,
        m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
        std::unique_ptr<iregistry_monitor_token>&           returned_ptr)
    {
        std::ignore = change_notification_ptr;
        std::ignore = path;
        returned_ptr.reset();

        M_VALIDATE_FLAGS_PARAMETER(
            flags,
            register_watch_flags::attribute_changes | register_watch_flags::key_changes |
                register_watch_flags::security_changes | register_watch_flags::value_changes |
                register_watch_flags::watch_subtree);

        auto notification_wrapper = std::unique_ptr<registry_monitor_change_notification_wrapper>(
            new registry_monitor_change_notification_wrapper(change_notification_ptr));

        auto d =
            m_underlying_registry_monitor->register_watch(flags,
                                                          path,
                                                          notification_wrapper.get(),
                                                          notification_wrapper->m_underlying_token);

        returned_ptr.reset(notification_wrapper.release());

        return d;
    }

} // namespace m::pil::impl::logging
