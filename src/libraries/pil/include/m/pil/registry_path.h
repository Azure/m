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
#include <vector>

#include <m/sstring/sstring.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include "common.h"
#include "disposition.h"
#include "registry_base_types.h"
#include "security_attributes.h"

namespace m::pil
{
    constexpr auto registry_delimiter   = '\\';   // char (MBCS Windows, UTF-8 Linux)
    constexpr auto wregistry_delimiter  = L'\\';  // wchar_t (UTF-16 Windows, UTF-32 Linux)
    constexpr auto u8registry_delimiter = u8'\\'; // char8_t (UTF-8)
    constexpr auto uregistry_delimiter  = u'\\';  // char16_t (UTF-16)
    constexpr auto Uregistry_delimiter  = U'\\';  // char32_t (UTF-32)

    enum class predefined_key : uint32_t
    {
        classes_root                = 0x80000000ul, // HKEY_CLASSES_ROOT
        current_user                = 0x80000001ul, // HKEY_CURRENT_USER
        local_machine               = 0x80000002ul, // HKEY_LOCAL_MACHINE
        users                       = 0x80000003ul, // HKEY_USERS
        performance_data            = 0x80000004ul, // HKEY_PERFORMANCE_DATA
        current_config              = 0x80000005ul, // HKEY_CURRENT_CONFIG
        current_user_local_settings = 0x80000007ul, // HKEY_CURRENT_USER_LOCAL_SETTINGS
        performance_text            = 0x80000050ul, // HKEY_PERFORMANCE_TEXT
        performance_nlstext         = 0x80000060ul, // HKEY_PERFORMANCE_NLSTEXT
    };

    std::optional<predefined_key>
    try_map_string_to_predefined_key(m::u16sstring str);

    std::optional<m::u16sstring>
    map_predefined_key_to_string(std::optional<predefined_key> pk);

    m::u16sstring
    map_predefined_key_to_string(predefined_key pk);

    namespace registry
    {
        // Intended to mimic std::filesystem::path, but for registry keys.
        class path
        {
        public:
            using char_type   = char16_t;
            using value_type  = char_type;
            using string_type = m::basic_sstring<char_type>;
            using view_type   = std::basic_string_view<char_type>;

            path() = default;

            path(string_type&& str);

            path(path const& other);

            path(path&& other) noexcept;

            path(view_type str);

            template <typename CharT>
                requires(m::character<CharT>)
            path(std::basic_string_view<CharT> value):
                m_value(m::to_string_view_t<char_type>(value))
            {}

            template <typename CharT>
                requires(m::character<CharT>)
            path(CharT const* ptr): path(std::basic_string_view<CharT>(ptr))
            {}

            path(predefined_key pk);

            path&
            operator=(path const& other);

            path&
            operator=(path&& other) noexcept;

            path&
            operator=(string_type&& str);

            path&
            operator=(view_type str);

            /// <summary>
            /// Supports:
            /// 
            /// registry_path /= additional_segments
            /// 
            /// </summary>
            /// <param name="other"></param>
            /// <returns></returns>
            path&
            operator/=(path const& other);

            path&
            operator/=(view_type v);

            friend path
            operator/(path const& left, path const& right);

            path
            operator+(path const& right) const;

            path
            operator+(std::optional<path> const& right) const;

            path
            operator+(view_type right) const;

            template <typename CharT>
            path&
            operator=(std::basic_string_view<CharT> view)
            {
                m_value = m::to_u16string(view);
                return *this;
            }

            value_type const*
            c_str() const noexcept;

            string_type const&
            native() const& noexcept;

            string_type const&
            native() const&& = delete;

            operator string_type() const;

            explicit
            operator std::optional<string_type>() const;

            string_type
            string() const;

            void
            clear();

            void
            swap(path& other) noexcept
            {
                using std::swap;
                swap(m_root_key, other.m_root_key);
                swap(m_value, other.m_value);
            }

            constexpr std::optional<predefined_key>
            root_key() const
            {
                return m_root_key;
            }

            path
            root() const;

            path
            parent_path() const;

            bool
            has_parent_path() const;

            std::pair<std::optional<path>, path>
            split_parent_path_and_leaf_name() const;

            string_type
            relative_path() const;

        private:
            //
            // Registry path state management
            //
            // If the path is "absolute", then m_root_key will have a value which is
            // the predefined_key which is the root of the path.
            //
            // m_value will be the relative path from the root to the key. There
            // will be no leading path separator, there will be no repeated path
            // separators, and no trailing path separator.
            //
            void
            try_parse(string_type&& in_str, std::optional<predefined_key>& root, string_type& value);

            void
            try_parse(view_type in_str, std::optional<predefined_key>& root, string_type& value);

            void
            validate_and_map_path(view_type in_str, string_type& value);

            void
            validate_and_map_path(string_type&& in_str, string_type& value);

            path(std::optional<predefined_key> root, string_type const& value);

            std::optional<predefined_key> m_root_key;
            string_type                   m_value;
        };
    } // namespace registry
} // namespace m::pil
