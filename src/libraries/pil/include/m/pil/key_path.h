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

#if 0
#include "common.h"
#include "disposition.h"
#include "registry_base_types.h"
#include "security_attributes.h"
#endif

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

    // Intended to mimic std::filesystem::path, but for registry keys.
    class key_path
    {
    public:
        using char_type   = char16_t;
        using value_type  = char_type;
        using string_type = m::basic_sstring<char_type>;
        using view_type   = std::basic_string_view<char_type>;

        key_path() = default;

        key_path(string_type&& str);

        key_path(key_path const& other);

        key_path(key_path&& other) noexcept;

        key_path(view_type str);

        template <typename CharT>
            requires(m::character<CharT>)
        key_path(std::basic_string_view<CharT> value):
            m_value(m::to_string_view_t<char_type>(value))
        {}

        template <typename CharT>
            requires(m::character<CharT>)
        key_path(CharT const* ptr): key_path(std::basic_string_view<CharT>(ptr))
        {}

        key_path(predefined_key pk);

        key_path&
        operator=(key_path const& other);

        key_path&
        operator=(key_path&& other) noexcept;

        key_path&
        operator=(string_type&& str);

        key_path&
        operator=(view_type str);

        bool
        operator==(key_path const& r) const;

        /// <summary>
        /// Supports:
        ///
        /// key_path /= additional_segments
        ///
        /// </summary>
        /// <param name="other"></param>
        /// <returns></returns>
        key_path&
        operator/=(key_path const& other);

        key_path&
        operator/=(view_type v);

        friend key_path
        operator/(key_path const& left, key_path const& right);

        key_path
        operator+(key_path const& right) const;

        key_path
        operator+(std::optional<key_path> const& right) const;

        key_path
        operator+(view_type right) const;

        template <typename CharT>
        key_path&
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
        swap(key_path& other) noexcept
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

        key_path
        root() const;

        key_path
        parent_path() const;

        bool
        has_parent_path() const;

        std::pair<std::optional<key_path>, key_path>
        split_parent_path_and_leaf_name() const;

        string_type
        relative_path() const;

        static string_type
        canonicalize_path_string(view_type v);

        static bool
        is_path_string_canonical(view_type v);

    private:
        //
        // Registry key_path state management
        //
        // If the key_path is "absolute", then m_root_key will have a value which is
        // the predefined_key which is the root of the key_path.
        //
        // m_value will be the relative key_path from the root to the key. There
        // will be no leading key_path separator, there will be no repeated key_path
        // separators, and no trailing key_path separator.
        //
        void
        try_parse(string_type&& in_str, std::optional<predefined_key>& root, string_type& value);

        void
        try_parse(view_type in_str, std::optional<predefined_key>& root, string_type& value);

        void
        validate_and_map_path(view_type in_str, string_type& value);

        void
        validate_and_map_path(string_type&& in_str, string_type& value);

        key_path(std::optional<predefined_key> root, string_type const& value);

        std::optional<predefined_key> m_root_key;
        string_type                   m_value;
    };
} // namespace m::pil
