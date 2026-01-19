// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "platform.h"

namespace m::pil
{
    std::unique_ptr<iregistry_monitor_token>
    registry_monitor::do_register_watch(
        register_watch_flags                                flags,
        pil::key_path const&                                key_path,
        m::not_null<iregistry_monitor_change_notification*> change_notification_ptr)
    {
        M_VALIDATE_FLAGS_PARAMETER(
            flags,
            register_watch_flags::attribute_changes | register_watch_flags::key_changes |
                register_watch_flags::security_changes | register_watch_flags::value_changes |
                register_watch_flags::watch_subtree);

        iregistry_monitor::register_watch_flags inner_flags{};

        if (!!(flags & register_watch_flags::attribute_changes))
            inner_flags |= iregistry_monitor::register_watch_flags::attribute_changes;

        if (!!(flags & register_watch_flags::key_changes))
            inner_flags |= iregistry_monitor::register_watch_flags::key_changes;

        if (!!(flags & register_watch_flags::security_changes))
            inner_flags |= iregistry_monitor::register_watch_flags::security_changes;

        if (!!(flags & register_watch_flags::value_changes))
            inner_flags |= iregistry_monitor::register_watch_flags::value_changes;

        if (!!(flags & register_watch_flags::watch_subtree))
            inner_flags |= iregistry_monitor::register_watch_flags::watch_subtree;

        auto l = std::unique_lock(m_mutex);

        return m_iregistry_monitor->register_watch(inner_flags, key_path, change_notification_ptr);
    }

} // namespace m::pil
