// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/key_path.h>
#include <m/strings/convert.h>
#include <m/utility/enum_operations.h.h>
#include <m/utility/utility.h>

#include "common.h"
#include "disposition.h"
#include "security_attributes.h"

namespace m::pil
{
    class registry_class;

    class key
    {
    public:
        using path_type = m::pil::key_path;

        key() = default;
        key(key const& other);
        key(key&& other) noexcept;
        ~key() = default;
        key&
        operator=(key const& other);
        key&
        operator=(key&& other) noexcept;

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;
            swap(l.m_key, r.m_key);
        }

        template <typename CharT>
        key
        create_key(std::basic_string_view<CharT> key_name)
        {
            return create_key(path_type(key_name));
        }

        key
        create_key(path_type const& key_name)
        {
            return do_create_key(key_name);
        }

        template <typename CharT>
        void
        delete_key(std::basic_string_view<CharT> key_name)
        {
            delete_key(path_type(key_name));
        }

        void
        delete_key(path_type const& key_name)
        {
            do_delete_key(key_name);
        }

        template <typename CharT>
        void
        delete_tree(std::basic_string_view<CharT> key_name)
        {
            delete_tree(path_type(key_name));
        }

        void
        delete_tree(path_type const& key_name)
        {
            do_delete_tree(key_name);
        }

        std::vector<path_type>
        list_subkey_names();

        void
        flush();

        template <typename CharT>
        key
        open_key(std::basic_string_view<CharT> key_name)
        {
            return do_open_key(path_type(key_name));
        }

        key
        open_key(path_type const& key_name)
        {
            return do_open_key(key_name);
        }

        time_point
        last_write_time();

        template <typename CharT>
        void
        rename_key(std::basic_string_view<CharT> old_key_name,
                   std::basic_string_view<CharT> new_key_name)
        {
            do_rename_key(m::to_u16string(old_key_name), m::to_u16string(new_key_name));
        }

        template <typename CharT>
        void
        rename_key(std::basic_string_view<CharT> new_key_name)
        {
            do_rename_key(m::to_u16string(new_key_name));
        }

        template <typename CharT>
        void
        delete_value(std::basic_string_view<CharT> value_name)
        {
            do_delete_value(m::to_u16string(value_name));
        }

        struct value_name_and_type
        {
            registry_string_type m_value_name{};
            reg_value_type       m_reg_value_type{};
        };

        std::vector<value_name_and_type>
        list_value_names_and_types();

        template <typename CharT>
        reg_value_type
        get_value_type(std::basic_string_view<CharT> value_name)
        {
            return do_get_value_type(m::to_u16string(value_name));
        }

        template <typename CharT>
        registry_string_type
        get_string_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_string_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        registry_string_type
        get_expand_string_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_expand_string_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        std::vector<registry_string_type>
        get_multi_string_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_multi_string_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        uint32_t
        get_uint32_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_uint32_value(m::to_u16string(value_name));
        }

        struct unmapped_value
        {
            reg_value_type         m_type;
            std::vector<std::byte> m_value;
        };

        struct string_value
        {
            registry_string_type m_value;
        };

        struct expand_string_value
        {
            registry_string_type m_value;
        };

        struct multi_string_value
        {
            std::vector<registry_string_type> m_value;
        };

        struct uint32_value
        {
            uint32_t m_value;
        };

        struct binary_value
        {
            std::vector<std::byte> m_value;
        };

        using registry_value = std::variant<unmapped_value,
                                            string_value,
                                            expand_string_value,
                                            multi_string_value,
                                            uint32_value,
                                            binary_value>;

        template <typename CharT>
        registry_value
        get_value(std::basic_string_view<CharT> value_name)
        {
            return do_value(to_u16string(value_name));
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT> value_name, registry_string_view_type value)
        {
            registry_string_type s(value);
            do_set_value(to_u16string(value_name), string_value{s});
        }

        template <typename CharT1, typename CharT2>
        void
        set_string_value(std::basic_string_view<CharT1> value_name,
                         std::basic_string_view<CharT2> value)
        {
            auto const                s = to_null_terminated_registry_storage_string(value);
            storage_string_value_view v{s};
            do_set_value(m::to_u16string(value_name), v);
        }

        template <typename CharT1, typename CharT2>
        void
        set_expand_string_value(std::basic_string_view<CharT1> value_name,
                                std::basic_string_view<CharT2> value)
        {
            registry_string_type s{to_null_terminated_registry_string(value)};
            expand_string_value  v{s};
            do_set_value(to_u16string(value_name), v);
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT>                 value_name,
                  std::vector<registry_string_view_type> const& value)
        {
            std::vector<registry_string_type> v(value.size());

            for (auto&& e: value)
                v.emplace_back(e);

            do_set_value(to_u16string(value_name), multi_string_value{std::move(v)});
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT> value_name, uint32_t value)
        {
            do_set_value(to_u16string(value_name), uint32_value{value});
        }

        path_type
        get_path()
        {
            return do_get_path();
        }

        key(std::shared_ptr<ikey>&& key) noexcept;
        key(std::shared_ptr<ikey> const& key);

    private:
        key
        do_create_key(path_type const& key_name);

        void
        do_delete_key(path_type const& key_name);

        void
        do_delete_tree(std::optional<path_type> const& key_name);

        key
        do_open_key(std::optional<path_type> const& key_name);

        void
        do_rename_key(path_type const& old_key_name, path_type const& new_key_name);

        void
        do_rename_key(path_type const& new_key_name);

        void
        do_delete_value(std::u16string_view value_name);

        reg_value_type
        do_get_value_type(std::u16string_view value_name);

        registry_string_type
        do_get_string_value(std::u16string_view value_name);

        std::vector<registry_string_type>
        do_get_multi_string_value(std::u16string_view value_name);

        registry_string_type
        do_get_expand_string_value(std::u16string_view value_name);

        uint32_t
        do_get_uint32_value(std::u16string_view value_name);

        registry_value
        do_get_value(std::u16string_view value_name);

        path_type
        do_get_path();

        //
        // For the storage views, we require that the callers have placed a
        // null character in the last position on the call into the do_set_*
        // function.
        //
        struct storage_string_value_view
        {
            registry_storage_string_view_type m_value;
        };

        struct storage_expand_string_value_view
        {
            registry_storage_string_view_type m_value;
        };

        //
        // A storage_multi_string_value_view is the encoded REG_MULTI_SZ
        // format with embedded null characters between values in the sequence
        // ending with a zero length value ("two null characters in a row").
        //
        struct storage_multi_string_value_view
        {
            registry_storage_string_view_type m_value;
        };

        struct storage_uint32_value
        {
            uint32_t m_value;
        };

        void
        do_set_value(std::u16string_view value_name, storage_string_value_view const& value);

        void
        do_set_value(std::u16string_view value_name, storage_expand_string_value_view const& value);

        void
        do_set_value(std::u16string_view value_name, storage_multi_string_value_view const& value);

        void
        do_set_value(std::u16string_view value_name, storage_uint32_value const& value);

        void
        get_value_into_byte_vector(std::u16string_view     value_name,
                                   reg_value_type&         value_type_out,
                                   std::vector<std::byte>& byte_vector);

        struct bytes_and_value_type
        {
            std::vector<std::byte> m_bytes;
            reg_value_type         m_type;
        };

        bytes_and_value_type
        get_value_as_bytes_and_value_type(std::u16string_view value_name);

        // If vt and s can't make a rational UTF-16 string, throws an
        // exception. Otherwise returns a string view which does not extend
        // lifetime over s's.
        static std::u16string_view
        try_interpret_span_as_utf16(reg_value_type                                  vt,
                                    std::span<std::byte const, std::dynamic_extent> s);

        template <typename CharT>
        static std::optional<std::u16string>
        to_u16string(std::optional<std::basic_string_view<CharT>> const& ov)
        {
            if (!ov.has_value())
                return std::nullopt;

            return m::to_u16string(ov.value());
        }

        template <typename CharT>
        static std::optional<std::u16string>
        to_u16string(std::basic_string_view<CharT> const& v)
        {
            return m::to_u16string(v);
        }

        std::shared_ptr<ikey> m_key;
    };

    class registry_monitor
    {
    public:
        registry_monitor() = default;

        registry_monitor(registry_monitor const& other) = delete;
        registry_monitor(registry_monitor&& other)      = delete;

        registry_monitor&
        operator=(registry_monitor const& other) = delete;

        registry_monitor&
        operator=(registry_monitor&& other) = delete;

        void
        swap(registry_monitor& other) = delete;

        /// <summary>
        /// Defines flags for monitoring changes to a registry key.
        ///
        /// Note that when `watch_subtree` is selected, the notification received does not
        /// indicate which key is modified, only that some key is modified.
        /// </summary>
        enum class register_watch_flags
        {
            watch_subtree     = 1 << 0,
            key_changes       = 1 << 1,
            attribute_changes = 1 << 2,
            value_changes     = 1 << 3,
            security_changes  = 1 << 4,
        };

        std::unique_ptr<iregistry_monitor_token>
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                          key_path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr)
        {
            return do_register_watch(flags, key_path, change_notification_ptr);
        }

    protected:
        registry_monitor(std::shared_ptr<pil::iregistry_monitor> sp):
            m_iregistry_monitor(std::move(sp))
        {}

        std::unique_ptr<iregistry_monitor_token>
        do_register_watch(
            register_watch_flags                                flags,
            pil::key_path const&                          key_path,
            m::not_null<iregistry_monitor_change_notification*> change_notification_ptr);

        std::mutex                              m_mutex;
        std::shared_ptr<pil::iregistry_monitor> m_iregistry_monitor;

        friend class registry_class;
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(registry_monitor::register_watch_flags);

    class registry_class
    {
    public:
        registry_class() = default;
        registry_class(registry_class const& other);
        registry_class(registry_class&& other) noexcept;
        registry_class(std::shared_ptr<iregistry>&&) noexcept;
        ~registry_class() = default;
        registry_class&
        operator=(registry_class const& other);
        registry_class&
        operator=(registry_class&&) noexcept;

        void
        swap(registry_class& other) noexcept;

        registry_monitor
        monitor() const;

        key
        open_predefined_key(predefined_key pk) const;

    private:
        std::shared_ptr<iregistry>
                                   get_registry() const;
        mutable std::mutex         m_mutex;
        std::shared_ptr<iregistry> m_registry;
    };

} // namespace m::pil
