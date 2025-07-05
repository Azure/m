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

#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include "common.h"
#include "disposition.h"
#include "security_attributes.h"

#include "registry_base_types.h"
#include "registry_interfaces.h"

namespace m::pil::registry
{
    class key
    {
    public:
        key() = default;
        key(key const& other);
        key(key&& other) noexcept;
        ~key() = default;
        key&
        operator=(key const& other);
        key&
        operator=(key&& other);

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
            return do_create_key(m::to_u16string(key_name));
        }

        template <typename CharT>
        void
        delete_key(std::basic_string_view<CharT> key_name)
        {
            do_delete_key(m::to_u16string(key_name));
        }

        template <typename CharT>
        void
        delete_tree(std::basic_string_view<CharT> key_name)
        {
            do_delete_tree(m::to_u16string(key_name));
        }

        std::vector<registry_string>
        list_subkey_names();

        void
        flush();

        template <typename CharT>
        key
        open_key(std::basic_string_view<CharT> key_name)
        {
            return do_open_key(m::to_u16string(key_name));
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
            registry_string m_value_name;
            value_type      m_type;
        };

        std::vector<value_name_and_type>
        list_value_names_and_types();

        template <typename CharT>
        value_type
        get_value_type(std::basic_string_view<CharT> value_name)
        {
            return do_get_value_type(m::to_u16string(value_name));
        }

        template <typename CharT>
        registry_string
        get_string_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_string_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        registry_string
        get_expand_string_value(std::basic_string_view<CharT> value_name)
        {
            return do_get_expand_string_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        std::vector<registry_string>
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
            value_type             m_type;
            std::vector<std::byte> m_value;
        };

        struct string_value
        {
            registry_string m_value;
        };

        struct expand_string_value
        {
            registry_string m_value;
        };

        struct multi_string_value
        {
            std::vector<registry_string> m_value;
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
            return do_value(m::to_u16string(value_name));
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT> value_name, registry_string_view value)
        {
            registry_string s(value);
            do_set_value(m::to_u16string(value_name), string_value{s});
        }

        template <typename CharT1, typename CharT2>
        void
        set_string_value(std::basic_string_view<CharT1> value_name,
                         std::basic_string_view<CharT2> value)
        {
            registry_storage_string   s{to_null_terminated_registry_storage_string(value)};
            storage_string_value_view v{s};
            do_set_value(m::to_u16string(value_name), v);
        }

        template <typename CharT1, typename CharT2>
        void
        set_expand_string_value(std::basic_string_view<CharT1> value_name,
                                std::basic_string_view<CharT2> value)
        {
            registry_storage_string          s{to_null_terminated_registry_storage_string(value)};
            storage_expand_string_value_view v{s};
            do_set_value(m::to_u16string(value_name), v);
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT>            value_name,
                  std::vector<registry_string_view> const& value)
        {
            std::vector<registry_string> v(value.size());

            for (auto&& e: value)
                v.emplace_back(e);

            do_set_value(m::to_u16string(value_name), multi_string_value{std::move(v)});
        }

        template <typename CharT>
        void
        set_value(std::basic_string_view<CharT> value_name, uint32_t value)
        {
            do_set_value(m::to_u16string(value_name), uint32_value{value});
        }

    private:
        key(std::shared_ptr<interfaces::key>&& key);
        key(std::shared_ptr<interfaces::key> const& key);

        key
        do_create_key(std::u16string_view key_name);

        void
        do_delete_key(std::u16string_view key_name);

        void
        do_delete_tree(std::u16string_view key_name);

        key
        do_open_key(std::u16string_view key_name);

        void
        do_rename_key(std::u16string_view old_key_name, std::u16string_view new_key_name);

        void
        do_rename_key(std::u16string_view new_key_name);

        void
        do_delete_value(std::u16string_view value_name);

        value_type
        do_get_value_type(std::u16string_view value_name);

        registry_string
        do_get_string_value(std::u16string_view value_name);

        std::vector<registry_string>
        do_get_multi_string_value(std::u16string_view value_name);

        registry_string
        do_get_expand_string_value(std::u16string_view value_name);

        uint32_t
        do_get_uint32_value(std::u16string_view value_name);

        registry_value
        do_get_value(std::u16string_view value_name);

        //
        // For the storage views, we require that the callers have placed a
        // null character in the last position on the call into the do_set_*
        // function.
        //
        struct storage_string_value_view
        {
            registry_storage_string_view m_value;
        };

        struct storage_expand_string_value_view
        {
            registry_storage_string_view m_value;
        };

        //
        // A storage_multi_string_value_view is the encoded REG_MULTI_SZ
        // format with embedded null characters between values in the sequence
        // ending with a zero length value ("two null characters in a row").
        //
        struct storage_multi_string_value_view
        {
            registry_storage_string_view m_value;
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
                                   value_type&             value_type_out,
                                   std::vector<std::byte>& byte_vector);

        struct bytes_and_value_type
        {
            std::vector<std::byte> m_bytes;
            value_type             m_type;
        };

        bytes_and_value_type
        get_value_as_bytes_and_value_type(std::u16string_view value_name);

        // If vt and s can't make a rational UTF-16 string, throws an
        // exception. Otherwise returns a string view which does not extend
        // lifetime over s's.
        static std::u16string_view
        try_interpret_span_as_utf16(value_type                                      vt,
                                    std::span<std::byte const, std::dynamic_extent> s);

        std::shared_ptr<interfaces::key> m_key;
    };
} // namespace m::pil::registry