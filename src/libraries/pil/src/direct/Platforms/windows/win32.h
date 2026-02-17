// Copyright (c) Microsoft Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry.h>
#include <m/pil/registry_interfaces.h>
#include <m/threadpool/threadpool.h>
#include <m/utility/locked.h>
#include <m/win32/registry.h>
#include <m/win32/threadpool.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::pil::impl::win32
{
    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;

        platform(std::shared_ptr<m::work_queue> wq);

        platform(platform const&)           = delete;
        platform(platform&& other) noexcept = delete;
        ~platform()                         = default;

        platform&
        operator=(platform const&) = delete;

        platform&
        operator=(platform&& other) noexcept = delete;

        void
        swap(platform& other) noexcept = delete;

        // Implementation of iplatform:

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override;

    private:
        std::mutex                     m_mutex;
        std::shared_ptr<m::work_queue> m_work_queue;
    };

    constexpr m::win32::registry::predefined_key
    pil_pk_to_win32_pk(m::pil::predefined_key pk)
    {
        switch (pk)
        {
            using enum m::pil::predefined_key;

            case classes_root: return m::win32::registry::predefined_key::classes_root;
            case current_user: return m::win32::registry::predefined_key::current_user;
            case local_machine: return m::win32::registry::predefined_key::local_machine;
            case users: return m::win32::registry::predefined_key::users;
            case performance_data: return m::win32::registry::predefined_key::performance_data;
            case current_config: return m::win32::registry::predefined_key::current_config;
            case current_user_local_settings:
                return m::win32::registry::predefined_key::current_user_local_settings;
            case performance_text: return m::win32::registry::predefined_key::performance_text;
            case performance_nlstext:
                return m::win32::registry::predefined_key::performance_nlstext;
            default: M_UNREACHABLE_CODE();
        }
    }

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;

        registry(std::shared_ptr<m::work_queue> wq);

        registry(registry const&)           = delete;
        registry(registry&& other) noexcept = delete;
        ~registry()                         = default;

        registry&
        operator=(registry const&) = delete;

        // move assignment does not make sense in light of enable_shared_from_this
        registry&
        operator=(registry&& other) noexcept = delete;

        // swap not compatible with enable_shared_from_this
        void
        swap(registry& other) noexcept = delete;

        open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags flags,
                            predefined_key            pk,
                            sam                       sam_desired,
                            std::shared_ptr<ikey>&    returned_key) override;

        monitor_disposition
        monitor(monitor_flags                               flags,
                std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor) override;

    private:
        void initialize_monitor(m::locked_t);

        std::mutex                         m_mutex;
        std::shared_ptr<work_queue>        m_work_queue;
        std::shared_ptr<iregistry_monitor> m_monitor;
    };

    std::shared_ptr<ikey>
    make_predefined_key(predefined_key k);

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key()                 = default;
        key(key const& other) = delete;
        key(key&& other) noexcept;
        key(m::win32::registry::hkey&& hk, m::pil::key_path pth);
        ~key() = default;
        key&
        operator=(key const& other) = delete;
        key&
        operator=(key&& other) noexcept = delete;

        void
        swap(key& other) noexcept = delete;

        create_key_disposition
        create_key(create_key_flags                   flags,
                   pil::key_path const&               name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        delete_key_disposition
        delete_key(delete_key_flags flags, pil::key_path const& name, sam sam_desired) override;

        delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::optional<pil::key_path> const& name) override;

        enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                     flags,
                       std::size_t                                    index,
                       std::span<pil::key_path, std::dynamic_extent>& key_names) override;

        flush_disposition
        flush(flush_flags flags) override;

        open_key_disposition
        open_key(open_key_flags                      flags,
                 std::optional<pil::key_path> const& key_name,
                 sam                                 sam_desired,
                 std::shared_ptr<ikey>&              returned_key) override;

        query_information_key_disposition
        query_information_key(query_information_key_flags flags,
                              std::size_t&                subkey_count,
                              std::size_t&                value_count,
                              std::size_t&                security_descriptor_size,
                              m::pil::time_point_type&    last_write_time) override;

        rename_key_disposition
        rename_key(rename_key_flags                    flags,
                   std::optional<pil::key_path> const& old_name,
                   pil::key_path const&                new_name) override;

        delete_value_disposition
        delete_value(delete_value_flags flags, value_name_string_type const& value_name) override;

        enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(enumerate_value_names_and_types_flags flags,
                                        std::size_t                           index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>&       values_span) override;

        get_value_size_disposition
        get_value_size(get_value_size_flags          flags,
                       value_name_string_type const& value_name,
                       std::size_t&                  size) override;

        get_value_type_disposition
        get_value_type(get_value_type_flags          flags,
                       value_name_string_type const& value_name,
                       reg_value_type&               type) override;

        get_value_disposition
        get_value(get_value_flags               flags,
                  value_name_string_type const& value_name,
                  reg_value_type&               type,
                  std::span<std::byte>&         value,
                  std::optional<std::size_t>&   new_bytes_required) override;

        set_value_disposition
        set_value(set_value_flags               flags,
                  value_name_string_type const& value_name,
                  reg_value_type                type,
                  std::span<std::byte const>    value) override;

        get_path_disposition
        get_path(get_path_flags flags, m::pil::key_path& path_out) override;

    private:
        m::win32::registry::hkey m_hkey;
        m::pil::key_path         m_path;
    };

    class registry_monitor :
        public m::pil::iregistry_monitor,
        public std::enable_shared_from_this<m::pil::impl::win32::registry_monitor>
    {
    public:
        registry_monitor() = default;
        registry_monitor(std::shared_ptr<m::work_queue> wq);
        registry_monitor(registry_monitor const& other)     = delete;
        registry_monitor(registry_monitor&& other) noexcept = delete;

        registry_monitor&
        operator=(registry_monitor const& other) = delete;

        registry_monitor&
        operator=(registry_monitor&& other) = delete;

        ~registry_monitor() = default;

        // Cannot swap enable_shared_from_this<>.
        void
        swap(registry_monitor& other) = delete;

        register_watch_disposition
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                                key_name,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
                       std::unique_ptr<iregistry_monitor_token>&           returned_ptr) override;

    private:
        std::mutex                  m_mutex;
        std::shared_ptr<work_queue> m_work_queue;
    };

    using namespace m::win32::threadpool;
    using namespace m::win32::registry;

    class registry_monitor_token : public m::pil::iregistry_monitor_token
    {
    public:
        registry_monitor_token() = delete;
        registry_monitor_token(
            std::shared_ptr<m::work_queue>                      work_queue,
            m::pil::iregistry_monitor::register_watch_flags     flags,
            pil::key_path const&                                key_path,
            m::not_null<iregistry_monitor_change_notification*> change_notification_ptr);
        registry_monitor_token(registry_monitor_token const& other)     = delete;
        registry_monitor_token(registry_monitor_token&& other) noexcept = delete;
        ~registry_monitor_token()                                       = default;

        registry_monitor_token&
        operator=(registry_monitor_token&& other) noexcept = delete;

        registry_monitor_token&
        operator=(registry_monitor_token const& other) = delete;

        void
        swap(registry_monitor_token& other) noexcept = delete;

    private:
        static void __stdcall
        registry_notification_wait_callback(PTP_CALLBACK_INSTANCE Instance,
                                            PVOID                 Context,
                                            PTP_WAIT              Wait,
                                            TP_WAIT_RESULT        WaitResult);

        void
        on_registry_notification(bool timed_out);

        enum class drive_results
        {
            waiting,
            not_waiting,
        };

        void
        on_timer(m::locked_t, utc_time_point_type const& when) noexcept;

        void
        drive_state(m::locked_t, utc_time_point_type const& when) noexcept;

        // Avoid recursion or complexity by moving the logic for each step
        // forward into the drive_state_once() function, so that drive_state()
        // is simply a loop (logically - it's not actually a loop since it
        // will return when any deferrals are issued via the timers.)
        drive_results
        drive_state_once(m::locked_t, utc_time_point_type const& when) noexcept;

        enum class state
        {
            to_open_key,
            to_notify_change_key,
            waiting,
        };

        std::mutex                                      m_mutex;
        std::shared_ptr<work_queue>                     m_work_queue;
        m::pil::iregistry_monitor::register_watch_flags m_flags;
        m::win32::registry::notify_filters              m_filters;
        state                                           m_state{state::to_open_key};
        pil::key_path                                   m_key_path;
        m::u16sstring                                   m_key_name;
        hkey                                            m_hkey;
        m::win32::event                                 m_event;
        tp_wait                                         m_tp_wait;

        m::not_null<iregistry_monitor_change_notification*> m_change_notification_ptr;
        std::unique_ptr<timer>                              m_timer;
        std::unique_ptr<timer>                              m_notification_timer;
        utc_time_point_type                                 m_notification_time;
    };

} // namespace m::pil::impl::win32
