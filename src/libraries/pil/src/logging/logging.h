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

#include <pugixml.hpp>

#include "../pugihelp.h"

using namespace std::string_view_literals;

namespace m::pil::impl::logging
{
    class log;

    using key_path    = pil::key_path;
    using char_type   = typename key_path::char_type;
    using string_type = typename key_path::string_type;
    using view_type   = typename key_path::view_type;

    constexpr auto registry_path_separator    = u'\\';
    constexpr auto registry_path_separator_sv = u"\\"sv;

    constexpr auto npos = view_type::npos;

    static_assert(string_type::npos == view_type::npos);

    template <typename TChar1>
        requires(m::character<TChar1>)
    pugi::xml_attribute
    append_attribute(pugi::xml_node& n, std::basic_string_view<TChar1> name)
    {
        pugi::xml_attribute a;

        if constexpr (std::is_same_v<TChar1, pugi::char_t>)
        {
            a = n.append_attribute(name);
        }
        else
        {
            auto s = m::to_basic_string_t<pugi::char_t>(name);
            a      = n.append_attribute(pugi::string_view_t(s.data(), s.size()));
        }

        return a;
    }

    template <typename TChar1, typename TChar2>
        requires(m::character<TChar1> && m::character<TChar2>)
    void
    write_attribute(pugi::xml_node&                n,
                    std::basic_string_view<TChar1> name,
                    std::basic_string_view<TChar2> value)
    {
        if (value.size() != 0)
        {
            auto a = append_attribute(n, name);

            if constexpr (std::is_same_v<TChar2, pugi::char_t>)
            {
                a.set_value(value.data(), value.size());
            }
            else
            {
                auto value_string = m::to_basic_string_t<pugi::char_t>(value);
                a.set_value(value_string.data(), value_string.size());
            }
        }
    }

    template <typename TChar1, typename TCode, typename TFlags>
        requires(m::character<TChar1> && std::is_scoped_enum_v<TCode> &&
                 std::is_scoped_enum_v<TFlags>)
    void
    write_attribute(pugi::xml_node&                n,
                    std::basic_string_view<TChar1> name,
                    disposition<TCode, TFlags>     d)
    {
        if (d)
        {
            auto a = append_attribute(n, name);
            a.set_value(
                std::format("{},{:#x}", m::to_underlying(d.code()), m::to_underlying(d.flags()))
                    .c_str());
        }
    }

    template <typename TChar1>
        requires(m::character<TChar1>)
    void
    write_attribute(pugi::xml_node& n, std::basic_string_view<TChar1> name, key_path const& path)
    {
        auto a = append_attribute(n, name);
        a.set_value(m::to_string(path.native().view()).c_str());
    }

    template <typename TChar1>
        requires(m::character<TChar1>)
    void
    write_attribute(pugi::xml_node&                n,
                    std::basic_string_view<TChar1> name,
                    std::optional<key_path> const& path)
    {
        if (path.has_value())
        {
            auto a = append_attribute(n, name);
            a.set_value(
                pugi::string_view_t(m::to_basic_string_t<pugi::char_t>(path.value().native().view())));
        }
    }

    template <typename TChar1, typename TValue>
        requires(m::character<TChar1> && std::integral<TValue>)
    void
    write_hex_integer_attribute(pugi::xml_node& n, std::basic_string_view<TChar1> name, TValue v)
    {
        write_attribute(n, name, pugi::string_view_t(std::format(M_PUGIXML_T("{:#x}"), v)));
    }

    template <typename TChar1, typename TValue>
        requires(m::character<TChar1> && (std::integral<TValue> || std::is_enum_v<TValue>))
    void
    write_hex_attribute_omitting_default(pugi::xml_node&                n,
                                         std::basic_string_view<TChar1> name,
                                         TValue                         v,
                                         TValue                         default_to_omit = TValue{})
    {
        if (v != default_to_omit)
            write_attribute(
                n,
                name,
                pugi::string_view_t(std::format(M_PUGIXML_T("{:#x}"), static_cast<uintmax_t>(v))));
    }

    /// <summary>
    /// The `log_entry` class is the base class for entries in the
    /// log queue. (Technically, the queue is kept in a std::deque<>
    /// so that it can be iterated upon.)
    ///
    /// Functionally, it serves mostly to have a type that's the
    /// root of the type hierarchy, and it has virtual member
    /// functions for behaviors that are required across all
    /// log entries.
    ///
    /// Initially this is only the ability to store their contents into
    /// a Pugixml document tree.
    /// </summary>
    class log_entry
    {
    public:
        virtual ~log_entry() = default;

    protected:
        log_entry() = default;

        virtual void
        save(pugi::xml_node& parent) const = 0;

        friend class log;
    };

    class log : public std::enable_shared_from_this<log>
    {
    public:
        log() = default;

        template <typename T>
            requires(std::derived_from<T, log_entry>)
        void
        add(std::unique_ptr<T>& entry)
        {
            auto l = std::unique_lock(m_mutex);
            m_deque.emplace_back(std::move(entry));
        }

        void
        save(pugi::xml_node& log_node) const;

    private:
        mutable std::mutex                     m_mutex;
        std::deque<std::unique_ptr<log_entry>> m_deque; // a deque is used for iteratability
    };

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;
        registry(std::shared_ptr<iregistry> const& underlying_registry,
                 std::shared_ptr<log> const&       log_ptr);
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
        std::shared_ptr<log>                            m_log;
        std::shared_ptr<iregistry_monitor>              m_monitor;
        std::map<predefined_key, std::shared_ptr<ikey>> m_predefined_keys;
    };

    class create_key_log_entry : public log_entry
    {
    public:
        create_key_log_entry(key_path const&                    base_key_path,
                             ikey::create_key_flags             flags,
                             key_path const&                    subkey_path,
                             sam                                sam_desired,
                             std::optional<security_attributes> sa);

        void
        set_disposition(ikey::create_key_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        key_path                           m_base_key_path;
        ikey::create_key_flags             m_flags;
        key_path                           m_subkey_path;
        sam                                m_sam_desired;
        std::optional<security_attributes> m_sa;
        ikey::create_key_disposition       m_disposition;
    };

    class delete_key_log_entry : public log_entry
    {
    public:
        delete_key_log_entry(key_path const&        base_key_path,
                             ikey::delete_key_flags flags,
                             key_path const&        subkey_path,
                             sam                    sam_desired);

        void
        set_disposition(ikey::delete_key_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        key_path                     m_base_key_path;
        ikey::delete_key_flags       m_flags;
        key_path                     m_subkey_path;
        sam                          m_sam_desired;
        ikey::delete_key_disposition m_disposition;
    };

    class delete_tree_log_entry : public log_entry
    {
    public:
        delete_tree_log_entry(key_path const&                base_key_path,
                              ikey::delete_tree_flags        flags,
                              std::optional<key_path> const& subkey_path);

        void
        set_disposition(ikey::delete_tree_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        key_path                      m_base_key_path;
        ikey::delete_tree_flags       m_flags;
        std::optional<key_path>       m_subkey_path;
        ikey::delete_tree_disposition m_disposition;
    };

    class rename_key_log_entry : public log_entry
    {
    public:
        rename_key_log_entry(key_path const&                base_key_path,
                             ikey::rename_key_flags         flags,
                             std::optional<key_path> const& sub_key_name,
                             pil::key_path const&           new_key_name);

        void
        set_disposition(ikey::rename_key_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        key_path                     m_base_key_path;
        ikey::rename_key_flags       m_flags;
        std::optional<key_path>      m_sub_key_name;
        key_path                     m_new_key_name;
        ikey::rename_key_disposition m_disposition;
    };

    class delete_value_log_entry : public log_entry
    {
    public:
        delete_value_log_entry(key_path const&               base_key_path,
                               ikey::delete_value_flags      flags,
                               value_name_string_type const& value_name);

        void
        set_disposition(ikey::delete_value_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        key_path                       m_base_key_path;
        ikey::delete_value_flags       m_flags;
        value_name_string_type         m_value_name;
        ikey::delete_value_disposition m_disposition;
    };

    class set_value_log_entry : public log_entry
    {
    public:
        set_value_log_entry(key_path const&               base_key_path,
                            ikey::set_value_flags         flags,
                            value_name_string_type const& value_name,
                            reg_value_type                type,
                            std::span<std::byte const>    value);

        void
        set_disposition(ikey::set_value_disposition disposition);

        void
        save(pugi::xml_node& parent) const override;

    protected:
        void
        save_binary(pugi::xml_node& parent) const;

        static bool
        data_is_utf16(std::span<std::byte const> const& x);

        static void
        set_value_as_string(pugi::xml_attribute& attr, std::span<std::byte const> const& s);

        key_path                        m_base_key_path;
        ikey::set_value_flags           m_flags;
        value_name_string_type          m_value_name;
        reg_value_type                  m_type;
        m::unique_span<std::byte const> m_value;
        ikey::set_value_disposition     m_disposition;
    };

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = delete;
        key(std::shared_ptr<ikey> const& key, std::shared_ptr<log> const& log_ptr);
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
        std::shared_ptr<log>  m_log;
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
        std::shared_ptr<log>       m_log;
        std::shared_ptr<registry>  m_registry;
    };

} // namespace m::pil::impl::logging
