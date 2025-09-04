// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

using namespace std::string_view_literals;

namespace m::pil
{
    constexpr auto path_separator      = u'\\';
    constexpr auto path_separator_view = u"\\"sv;

    namespace registry_impl
    {
        using P = std::pair<m::u16sstring, predefined_key>;

        inline const std::map<m::u16sstring,
                              predefined_key,
                              m::case_insensitive_less<m::u16sstring>>
            predefined_key_names = {{
                P{u"HKCR", predefined_key::classes_root},
                P{u"HKEY_CLASSES_ROOT", predefined_key::classes_root},
                P{u"HKCU", predefined_key::current_user},
                P{u"HKEY_CURRENT_USER", predefined_key::current_user},
                P{u"HKLM", predefined_key::local_machine},
                P{u"HKEY_LOCAL_MACHINE", predefined_key::local_machine},
                P{u"HKEY_USERS", predefined_key::users},
                P{u"HKEY_PERFORMANCE_DATA", predefined_key::performance_data},
                P{u"HKEY_CURRENT_CONFIG", predefined_key::current_config},
                P{u"HKCC", predefined_key::current_config},
                P{u"HKEY_CURRENT_USER_LOCAL_SETTINGS", predefined_key::current_user_local_settings},
                P{u"HKEY_PERFORMANCE_TEXT", predefined_key::performance_text},
                P{u"HKEY_PERFORMANCE_NLSTEXT", predefined_key::performance_nlstext},
            }};

        using Q = std::pair<predefined_key, m::u16sstring>;

        /// <summary>
        /// pk_to_string_map is a map from the predefined_key values to canonically stored
        /// strings. This is used rather than a switch statement so that the common shared
        /// string values are returned instead of new copies.
        /// </summary>
        inline const std::map<predefined_key, m::u16sstring> pk_to_string_map = {{
            Q{predefined_key::classes_root, u"HKCR"},
            Q{predefined_key::current_user, u"HKCU"},
            Q{predefined_key::local_machine, u"HKLM"},
            Q{predefined_key::users, u"HKEY_USERS"},
            Q{predefined_key::performance_data, u"HKEY_PERFORMANCE_DATA"},
            Q{predefined_key::current_config, u"HKCC"},
            Q{predefined_key::current_user_local_settings, u"HKEY_CURRENT_USER_LOCAL_SETTINGS"},
            Q{predefined_key::performance_text, u"HKEY_PERFORMANCE_TEXT"},
            Q{predefined_key::performance_nlstext, u"HKEY_PERFORMANCE_NLSTEXT"},
        }};

    } // namespace registry_impl

    std::optional<predefined_key>
    try_map_string_to_predefined_key(m::u16sstring str)
    {
        auto const it = registry_impl::predefined_key_names.find(str);
        if (it == registry_impl::predefined_key_names.end())
            return std::nullopt;

        return it->second;
    }

    std::optional<m::u16sstring>
    map_predefined_key_to_string(std::optional<predefined_key> pk)
    {
        if (!pk.has_value())
            return std::nullopt;

        auto it = registry_impl::pk_to_string_map.find(pk.value());

        if (it == registry_impl::pk_to_string_map.end())
            return std::nullopt;

        return it->second;
    }

    m::u16sstring
    map_predefined_key_to_string(predefined_key pk)
    {
        auto it = registry_impl::pk_to_string_map.find(pk);

        if (it == registry_impl::pk_to_string_map.end())
        {
            throw m::invalid_parameter("pk");
        }

        return it->second;
    }

    key_path::key_path(key_path const& other): m_root_key(other.m_root_key), m_value(other.m_value)
    {}

    key_path::key_path(key_path::string_type&& str) { try_parse(str, m_root_key, m_value); }

    key_path::key_path(key_path&& other) noexcept
    {
        using std::swap;

        swap(m_root_key, other.m_root_key);
        swap(m_value, other.m_value);
    }

    key_path::key_path(predefined_key pk)
    {
        auto it = registry_impl::pk_to_string_map.find(pk);
        if (it == registry_impl::pk_to_string_map.end())
        {
            throw m::invalid_parameter("pk");
        }

        m_root_key = pk;
        m_value    = it->second;
    }

    key_path::key_path(view_type view) { try_parse(view, m_root_key, m_value); }

    key_path::key_path(std::optional<predefined_key> root, string_type const& value):
        m_root_key(root), m_value(value)
    {}

    key_path&
    key_path::operator=(key_path const& other)
    {
        m_root_key = other.m_root_key;
        m_value    = other.m_value;
        return *this;
    }

    key_path&
    key_path::operator=(key_path&& other) noexcept
    {
        using std::swap;

        swap(m_root_key, other.m_root_key);
        swap(m_value, other.m_value);

        return *this;
    }

    bool
    key_path::operator==(key_path const& r) const
    {
        if (m_root_key != r.m_root_key)
            return false;

        return m_value == r.m_value;
    }

    key_path
    key_path::operator+(view_type v) const
    {
        string_type value{{view_type{m_value}, path_separator_view, v}};
        return key_path(m_root_key, std::move(value));
    }

    key_path
    key_path::operator+(key_path const& r) const
    {
        return this->operator+(r.native().view());
    }

    key_path
    key_path::operator+(std::optional<key_path> const& r) const
    {
        if (r.has_value())
            return this->operator+(r.value());

        return *this;
    }

    key_path
    key_path::parent_path() const
    {
        if (auto const i = m_value.try_find_first_of(wregistry_delimiter); i.has_value())
            return key_path{m_root_key, m_value.substr(0, i.value())};

        return key_path{};
    }

    bool
    key_path::has_parent_path() const
    {
        return m_value.contains(wregistry_delimiter);
    }

    key_path::string_type
    key_path::relative_path() const
    {
        auto const seppos = m_value.try_find_first_of(path_separator);

        if (seppos.has_value())
            return m_value.substr(seppos.value() + 1);

        return string_type{};
    }

    key_path
    key_path::root() const
    {
        return key_path(m_root_key, m_value.substr(0, m_value.find_first_of(path_separator)));
    }

    key_path::operator string_type() const { return m_value; }

    key_path::operator std::optional<key_path::string_type>() const { return m_value; }

    key_path::string_type const&
    key_path::native() const& noexcept
    {
        return m_value;
    }

    key_path::string_type
    key_path::string() const
    {
        return m_value;
    }

    key_path::value_type const*
    key_path::c_str() const noexcept
    {
        return m_value.c_str();
    }

    void
    key_path::try_parse(key_path::string_type&&        in_str,
                        std::optional<predefined_key>& root_result,
                        key_path::string_type&         value_result)
    {
        root_result = try_map_string_to_predefined_key(
            in_str.substr(0, in_str.find_first_of(path_separator)));
        validate_and_map_path(std::move(in_str), value_result);
    }

    void
    key_path::try_parse(view_type                      in_str,
                        std::optional<predefined_key>& root_result,
                        key_path::string_type&         value_result)
    {
        root_result =
            try_map_string_to_predefined_key(in_str.substr(0, in_str.find(path_separator)));
        validate_and_map_path(in_str, value_result);
    }

    void
    key_path::validate_and_map_path(view_type in_str, string_type& value)
    {
        value = in_str;
    }

    void
    key_path::validate_and_map_path(string_type&& in_str, string_type& value)
    {
        value = std::move(in_str);
    }
} // namespace m::pil
