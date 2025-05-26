// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define WIL_ENABLE_EXCEPTIONS

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <string_view>

using namespace std::chrono_literals;

#include <m/utility/pointers.h>

#include "directory_watcher.h"

namespace m::filesystem_impl::platform_specific
{
    directory_watcher::directory_watcher(std::filesystem::path const& path):
        m_default_retry_delay(500ms), m_minimum_retry_delay(50ms), m_path(path), m_is_valid(true)
    {}

    std::unique_ptr<m::filesystem::change_notification_registration_token>
    directory_watcher::try_add_watch(uintmax_t                                        key,
                                     std::filesystem::path const&                     p,
                                     m::not_null<m::filesystem::change_notification*> ptr)
    {
        auto filename = p.filename();

        if (p.has_parent_path())
            throw std::runtime_error("path must be leaf only");

        auto l = std::unique_lock(m_mutex);

        // If the watcher has become invalid, return nullptr so that the monitor starts a new
        // watcher.
        if (!m_is_valid)
            return nullptr;

        auto token = std::make_unique<registration_token>(key, m::not_null(this));

        m_registered_watches.emplace_back(key, filename, ptr);

        ptr->on_begin();

        return token;
    }

    void
    directory_watcher::remove_watch(uintmax_t key)
    {
        auto l = std::unique_lock(m_mutex);

        auto result = std::ranges::find_if(m_registered_watches,
                                           [key](auto const& w) { return w.m_key == key; });
        if (result == m_registered_watches.end())
            throw std::runtime_error("No matching watch found for key");

        result->m_change_notification->on_cancelled();

        m_registered_watches.erase(result);
    }

    registration_token::registration_token(uintmax_t key, m::not_null<directory_watcher*> ptr):
        m_key(key), m_directory_watcher(ptr)
    {}

    registration_token::~registration_token() { m_directory_watcher->remove_watch(m_key); }

} // namespace m::filesystem_impl::platform_specific
