// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <new>
#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

#include "pcwstr.h"
#include "win32.h"

namespace m::pil::impl::win32
{
    registry_monitor::registry_monitor(std::shared_ptr<m::work_queue> wq):
        m_work_queue(std::move(wq))
    {}

    iregistry_monitor::register_watch_disposition
    registry_monitor::register_watch(
        register_watch_flags                                flags,
        pil::key_path const&                          key_path,
        m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
        std::unique_ptr<iregistry_monitor_token>&           returned_ptr)
    {
        returned_ptr.reset();

        M_VALIDATE_FLAGS_PARAMETER(
            flags,
            register_watch_flags::watch_subtree | register_watch_flags::key_changes |
                register_watch_flags::attribute_changes | register_watch_flags::value_changes |
                register_watch_flags::security_changes);

        auto l = std::unique_lock(m_mutex);

        auto wq = m_work_queue;

        l.unlock();

        returned_ptr.reset(
            new registry_monitor_token(m_work_queue, flags, key_path, change_notification_ptr));

        return register_watch_disposition{};
    }
} // namespace m::pil::impl::win32
