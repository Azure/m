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
    registry_class::registry_class(std::shared_ptr<iregistry>&& sp) noexcept:
        m_registry(std::move(sp))
    {}

    registry_class::registry_class(registry_class&& other) noexcept
    {
        using std::swap;

        swap(m_registry, other.m_registry);
    }

    registry_class::registry_class(registry_class const& other): m_registry(other.get_registry()) {}

    registry_class&
    registry_class::operator=(registry_class const& other)
    {
        auto registry = other.get_registry();
        auto l        = std::unique_lock(m_mutex);
        m_registry    = registry;
        return *this;
    }

    registry_class&
    registry_class::operator=(registry_class&& other) noexcept
    {
        using std::swap;

        swap(m_registry, other.m_registry);

        return *this;
    }

    std::shared_ptr<iregistry>
    registry_class::get_registry() const
    {
        auto l = std::unique_lock(m_mutex);
        return m_registry;
    }

    void
    registry_class::swap(registry_class& other) noexcept
    {
        using std::swap;
        swap(m_registry, other.m_registry);
    }

    key
    registry_class::open_predefined_key(predefined_key pk) const
    {
        auto l = std::unique_lock(m_mutex);
        return key(m_registry->open_predefined_key(pk));
    }

    registry_monitor
    registry_class::monitor() const
    {
        auto l = std::unique_lock(m_mutex);
        return registry_monitor(m_registry->monitor());
    }

} // namespace m::pil
