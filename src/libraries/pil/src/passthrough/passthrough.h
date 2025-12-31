// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/pil/registry_interfaces.h>
#include <m/strings/compare.h>
#include <m/utility/locked.h>

using namespace std::string_view_literals;

namespace m::pil::impl::passthrough
{
    using key_path    = pil::key_path;
    using char_type   = typename key_path::char_type;
    using string_type = typename key_path::string_type;
    using view_type   = typename key_path::view_type;

    constexpr auto registry_path_separator    = u'\\';
    constexpr auto registry_path_separator_sv = u"\\"sv;

    constexpr auto npos = view_type::npos;

    static_assert(string_type::npos == view_type::npos);

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;
        registry(std::shared_ptr<iregistry> const& underlying_registry);
        registry(registry&& other) noexcept = delete;
        registry(registry const&)           = delete;
        ~registry()                         = default;

        registry&
        operator=(registry&& other) noexcept = delete;

        registry&
        operator=(registry const&) = delete;

        void
        swap(registry& r) noexcept = delete;

        iregistry::open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags      flags,
                            predefined_key                 pk,
                            sam                            sam_desired,
                            std::shared_ptr<m::pil::ikey>& returned_key) override;

        monitor_disposition
        monitor(monitor_flags                               flags,
                std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor) override;

    protected:
        void initialize_monitor(m::locked_t);

        std::mutex                                      m_mutex;
        std::shared_ptr<iregistry>                      m_underlying_registry;
        std::shared_ptr<iregistry_monitor>              m_monitor;
        std::map<predefined_key, std::shared_ptr<ikey>> m_predefined_keys;
    };

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = delete;
        key(std::shared_ptr<ikey> const& key);
        key(key const& other)     = delete;
        key(key&& other) noexcept = delete;
        ~key()                    = default;

        key&
        operator=(key const& other) = delete;
        key&
        operator=(key&& other) noexcept = delete;

        void
        swap(key& other) noexcept = delete;

        ikey::create_key_disposition
        create_key(ikey::create_key_flags             flags,
                   key_path const&                    name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags flags, key_path const& name, sam sam_desired) override;

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags flags, std::optional<key_path> const& name) override;

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                flags,
                       std::size_t                               index,
                       std::span<key_path, std::dynamic_extent>& key_names) override;

        ikey::flush_disposition
        flush(ikey::flush_flags flags) override;

        ikey::open_key_disposition
        open_key(ikey::open_key_flags           flags,
                 std::optional<key_path> const& key_name,
                 sam                            sam_desired,
                 std::shared_ptr<ikey>&         returned_key) override;

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags flags,
                              std::size_t&                      subkey_count,
                              std::size_t&                      value_count,
                              std::size_t&                      security_descriptor_size,
                              time_point&                       last_write_time) override;

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags         flags,
                   std::optional<key_path> const& old_key_name,
                   key_path const&                new_key_name) override;

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags      flags,
                     value_name_string_type const& value_name) override;

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(ikey::enumerate_value_names_and_types_flags flags,
                                        std::size_t                                 index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>& values_span) override;

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags    flags,
                       value_name_string_type const& value_name,
                       std::size_t&                  size) override;

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags    flags,
                       value_name_string_type const& value_name,
                       reg_value_type&               type) override;

        ikey::get_value_disposition
        get_value(ikey::get_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type&               type,
                  std::span<std::byte>&         value,
                  std::optional<std::size_t>&   new_bytes_required) override;

        ikey::set_value_disposition
        set_value(ikey::set_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type                type,
                  std::span<std::byte const>    value) override;

        ikey::get_path_disposition
        get_path(ikey::get_path_flags flags, m::pil::key_path& path_out) override;

    private:
        std::shared_ptr<ikey> m_key;
    };

    class registry_monitor :
        public iregistry_monitor,
        public std::enable_shared_from_this<registry_monitor>
    {
    public:
        registry_monitor() = default;
        registry_monitor(std::shared_ptr<iregistry_monitor> const& underlying_registry_monitor);
        registry_monitor(registry_monitor&& other) noexcept = delete;
        registry_monitor(registry_monitor const&)           = delete;
        ~registry_monitor()                                 = default;

        registry_monitor&
        operator=(registry_monitor&& other) noexcept = delete;

        registry_monitor&
        operator=(registry_monitor const&) = delete;

        void
        swap(registry_monitor& other) noexcept = delete;

        register_watch_disposition
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                                key_path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
                       std::unique_ptr<iregistry_monitor_token>&           returned_ptr) override;

    private:
        std::shared_ptr<iregistry_monitor> m_underlying_registry_monitor;
    };

    class registry_monitor_change_notification_wrapper :
        public iregistry_monitor_change_notification,
        public iregistry_monitor_token
    {
    public:
        registry_monitor_change_notification_wrapper() = delete;
        registry_monitor_change_notification_wrapper(
            m::not_null<iregistry_monitor_change_notification*> change_notification);
        registry_monitor_change_notification_wrapper(
            registry_monitor_change_notification_wrapper const&) = delete;
        registry_monitor_change_notification_wrapper(
            registry_monitor_change_notification_wrapper&&) noexcept = delete;
        ~registry_monitor_change_notification_wrapper()              = default;

        registry_monitor_change_notification_wrapper&
        operator=(registry_monitor_change_notification_wrapper const&) = delete;

        registry_monitor_change_notification_wrapper&
        operator=(registry_monitor_change_notification_wrapper&&) noexcept = delete;

        void
        swap(registry_monitor_change_notification_wrapper& other) noexcept = delete;

        void
        on_begin(utc_time_point when) override;

        std::optional<requeue_key_access_attempt>
        on_key_access_failure(utc_time_point           when,
                              pil::key_path const&     key,
                              std::system_error const& ec) override;

        std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(utc_time_point           when,
                                               pil::key_path const&     key,
                                               std::system_error const& ec) override;

        void
        on_change(utc_time_point when, pil::key_path const& key) override;

        void
        on_cancelled(utc_time_point when) override;

        // protected:
        m::not_null<iregistry_monitor_change_notification*> m_change_notification;
        std::unique_ptr<iregistry_monitor_token>            m_underlying_token;
    };

    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;
        platform(std::shared_ptr<iplatform> const& underlying_platform);
        platform(platform&& other) noexcept = delete;
        platform(platform const&)           = delete;
        ~platform()                         = default;

        platform&
        operator=(platform&& other) noexcept = delete;

        platform&
        operator=(platform const&) = delete;

        void
        swap(platform& other) noexcept = delete;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override;

    protected:
        std::shared_ptr<iplatform> m_underlying_platform;
        std::shared_ptr<registry>  m_registry;
    };

} // namespace m::pil::impl::passthrough
