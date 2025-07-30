// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <numeric>
#include <ratio>
#include <tuple>
#include <type_traits>

#include <m/utility/pointers.h>

#include "monitor.h"

#ifdef WIN32
#include "platforms/windows/directory_watcher.h"
#else
#include "platforms/linux/directory_watcher.h"
#endif

namespace m::filesystem_impl
{
    monitor::monitor()
    {
        //
        // It's possible that the seed should come from a testing
        // environment for reproducibility. TBD.
        //
        // This could have been done in the constructor initializion list if
        // the std::random_device were a data member of the monitor but that
        // seemed wasteful.
        //
        // Instead since there's a need to manage the seed explicitly for
        // tests in any case; the seeding is done here and the constructor
        // is "poisoned" with a seed of 0 so that the exact same set of
        // "random" numbers are issued prior to construction.
        //
        std::random_device rd{};
        m_generator.seed(rd());
    }

    std::unique_ptr<m::filesystem::change_notification_registration_token>
    monitor::do_register_watch(
        std::filesystem::path const&                     path,
        m::not_null<m::filesystem::change_notification*> change_notification_ptr,
        std::initializer_list<watch_policy>&&            policies)
    {
        if (!path.is_absolute())
            throw std::runtime_error("Path must be absolute");

        std::ignore = policies;

        auto const parent   = path.parent_path();
        auto const filename = path.filename();

        auto l = std::unique_lock(m_mutex);
        // This pattern is hazardous and assumes that directories are NOT
        // removed from the list of directories watched which is not good.
        // Unfortunately to fix it would require making the list have
        // std::shared_ptr<>s instead of just plain directory_watcher
        // pointers which also seems heavyweight. Will have to be fixed
        // some day.
        auto const watcher = get_dir_watcher(parent);
        auto token = watcher->add_file_watch(
            m_generator(), filename, change_notification_ptr);
        l.unlock();
        watcher->ensure_watching();
        return token;
    }

    m::not_null<m::filesystem_impl::platform_specific::directory_watcher*>
    monitor::get_dir_watcher(std::filesystem::path const& path)
    {
        {
            auto it = m_watchers.find(path);
            if (it != m_watchers.end())
            {
                return it->second.get();
            }
        }

        auto [it, success] = m_watchers.emplace(std::make_pair(
            path,
            std::make_unique<m::filesystem_impl::platform_specific::directory_watcher>(path)));
        return it->second.get();
    }

} // namespace m::filesystem_impl

std::shared_ptr<m::filesystem::monitor>
m::filesystem::get_monitor()
{
    std::shared_ptr<m::filesystem::monitor> return_value{};

    return_value = m::filesystem_impl::monitor::ms_monitor.load(std::memory_order_acquire);
    if (return_value)
        return return_value;

    auto new_monitor = std::make_shared<m::filesystem_impl::monitor>();

    for (;;)
    {
        if (m::filesystem_impl::monitor::ms_monitor.compare_exchange_strong(
                return_value, new_monitor, std::memory_order_acq_rel))
        {
            break;
        }

        if (return_value)
            break;
    }

    return m::filesystem_impl::monitor::ms_monitor.load(std::memory_order_acquire);
}
