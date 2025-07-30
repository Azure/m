// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>

#include <m/filesystem/filesystem_monitor.h>
#include <m/utility/pointers.h>

#ifdef WIN32
#include "platforms/windows/directory_watcher.h"
#else
#include "platforms/linux/directory_watcher.h"
#endif

namespace m::filesystem_impl
{
    class monitor : public m::filesystem::monitor
    {
    public:
        monitor();
        ~monitor()              = default;
        monitor(monitor const&) = delete;

        void
        operator=(monitor const&) = delete;

    protected:
        std::unique_ptr<m::filesystem::change_notification_registration_token>
        do_register_watch(
            std::filesystem::path const&                       path,
            m::not_null<m::filesystem::change_notification*> ptr,
                          std::initializer_list<watch_policy>&& policies) override;

        friend class m::filesystem_impl::platform_specific::directory_watcher;

        m::not_null<m::filesystem_impl::platform_specific::directory_watcher*>
        get_dir_watcher(std::filesystem::path const& path);

        std::mutex m_mutex;

        // monitor() constructor manages the seed explicitly. Don't use before the
        // monitor() constructor runs.
        std::mt19937_64 m_generator{0};

        std::chrono::milliseconds m_policy_minimum_requeue_duration;

        // Maintain a single watcher per directory per monitor
        // For now there's no solution for how the watchers are removed
        // from this map which is an issue.
        std::map<std::filesystem::path, std::unique_ptr<m::filesystem_impl::platform_specific::directory_watcher>> m_watchers;

        inline static std::atomic<std::shared_ptr<m::filesystem::monitor>> ms_monitor;

        friend std::shared_ptr<m::filesystem::monitor>
        m::filesystem::get_monitor();
    };
} // namespace m::filesystem_impl
