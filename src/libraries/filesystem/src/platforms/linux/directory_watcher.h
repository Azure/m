// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <m/cast/to.h>
#include <m/filesystem/filesystem.h>
#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>

namespace m::filesystem_impl::platform_specific
{
    class directory_watcher;

    struct registration_token : public m::filesystem::change_notification_registration_token
    {
    public:
        registration_token(uintmax_t key, m::not_null<directory_watcher*> ptr);
        ~registration_token();

        uintmax_t                       m_key;
        m::not_null<directory_watcher*> m_directory_watcher;
    };

    class directory_watcher
    {
    public:
        directory_watcher(std::filesystem::path const& path);

        std::unique_ptr<m::filesystem::change_notification_registration_token>
        try_add_watch(uintmax_t                                        key,
                      std::filesystem::path const&                     p,
                      m::not_null<m::filesystem::change_notification*> change_notification_ptr);

        void
        remove_watch(uintmax_t key);

    protected:
        struct registered_watch
        {
            registered_watch(
                uintmax_t                                        key,
                std::filesystem::path const&                     name,
                m::not_null<m::filesystem::change_notification*> change_notification_ptr):
                m_key(key),
                m_name(name),
                m_change_notification(change_notification_ptr)
            {}

            uintmax_t             m_key;
            std::filesystem::path m_name;
            m::not_null<m::filesystem::change_notification*> m_change_notification;
        };

        std::mutex                              m_mutex;
        std::chrono::milliseconds               m_default_retry_delay;
        std::chrono::milliseconds               m_minimum_retry_delay;
        std::filesystem::path                   m_path;
        std::weak_ptr<m::filesystem::monitor>   m_monitor;
        std::vector<registered_watch>           m_registered_watches;
        bool                                    m_is_valid;
    };
} // namespace m::filesystem_impl::platform_specific
