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

#include "buffered_registry.h"

namespace m::pil::impl::buffered
{
    registry_monitor::registry_monitor(
        std::shared_ptr<iregistry_monitor> const& underlying_registry_monitor):
        m_underlying_registry_monitor(underlying_registry_monitor)
    {}

    registry_monitor::registry_monitor(
        std::shared_ptr<iregistry_monitor>&& underlying_registry_monitor) noexcept:
        m_underlying_registry_monitor(std::move(underlying_registry_monitor))
    {}

    iregistry_monitor::register_watch_disposition
    registry_monitor::register_watch(
        register_watch_flags                                flags,
        pil::registry::path const&                          path,
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

        M_NOT_IMPLEMENTED("buffered registry change notification not implemented");

        //return register_watch_disposition{};
    }

} // namespace m::pil::impl::buffered
