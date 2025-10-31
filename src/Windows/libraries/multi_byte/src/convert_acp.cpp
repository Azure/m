// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace m
{
    void
    to_string(std::u8string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u8string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::u16string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u16string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::u16string const& s, std::string& str)
    {
        to_acp_string(std::u16string_view{s}, str);
    }

    std::string
    to_string(std::u16string const& s)
    {
        std::string str;
        to_acp_string(std::u16string_view{s}, str);
        return str;
    }

    std::optional<std::string>
    to_string(std::optional<std::u16string> const& s)
    {
        if (!s.has_value())
            return std::nullopt;

        return to_string(s.value());
    }

    void
    to_string(std::u32string_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::u32string_view v)
    {
        return to_acp_string(v);
    }

    void
    to_string(std::wstring_view v, std::string& str)
    {
        to_acp_string(v, str);
    }

    std::string
    to_string(std::wstring_view v)
    {
        return to_acp_string(v);
    }

    void
    to_u8string(m::czstring szstr, std::optional<std::u8string>& str)
    {
        if (!szstr)
            str = std::nullopt;
        else
            to_u8string(m::not_null(szstr), str);
    }

    void
    to_u8string(m::not_null<m::czstring> szstr, std::u8string& str)
    {
        to_u8string(std::string_view(szstr), str);
    }

    std::optional<std::u8string>
    to_u8string(m::czstring szstr)
    {
        if (!szstr)
            return std::nullopt;

        return to_u8string(m::not_null(szstr));
    }

    std::u8string
    to_u8string(m::not_null<m::czstring> szstr)
    {
        return to_u8string(std::string_view(szstr));
    }

    void
    to_u8string(std::string_view v, std::u8string& str)
    {
        acp_to_u8string(v, str);
    }

    std::u8string
    to_u8string(std::string_view v)
    {
        return acp_to_u8string(v);
    }

    void
    to_u16string(m::czstring szstr, std::optional<std::u16string>& str)
    {
        if (!szstr)
            str = std::nullopt;
        else
            to_u16string(m::not_null(szstr), str);
    }

    void
    to_u16string(m::czstring szstr, std::u16string& str)
    {
        to_u16string(std::string_view(szstr), str);
    }

    std::optional<std::u16string>
    to_u16string(m::czstring szstr)
    {
        if (!szstr)
            return std::nullopt;

        return to_u16string(m::not_null(szstr));
    }

    std::u16string
    to_u16string(m::not_null<m::czstring> szstr)
    {
        return to_u16string(std::string_view(szstr));
    }

    void
    to_u16string(std::string_view v, std::u16string& str)
    {
        acp_to_u16string(v, str);
    }

    std::u16string
    to_u16string(std::string_view v)
    {
        return acp_to_u16string(v);
    }

    void
    to_u32string(m::czstring szstr, std::optional<std::u32string>& str)
    {
        if (!szstr)
            str = std::nullopt;
        else
            to_u32string(m::not_null(szstr), str);
    }

    void
    to_u32string(m::not_null<m::czstring> szstr, std::u32string& str)
    {
        to_u32string(std::string_view(szstr), str);
    }

    std::optional<std::u32string>
    to_u32string(m::czstring szstr)
    {
        if (!szstr)
            return std::nullopt;

        return to_u32string(m::not_null(szstr));
    }

    std::u32string
    to_u32string(m::not_null<m::czstring> szstr)
    {
        return to_u32string(std::string_view(szstr));
    }

    void
    to_u32string(std::string_view v, std::u32string& str)
    {
        acp_to_u32string(v, str);
    }

    std::u32string
    to_u32string(std::string_view v)
    {
        return acp_to_u32string(v);
    }

    void
    to_wstring(m::czstring szstr, std::optional<std::wstring>& str)
    {
        if (!szstr)
            str = std::nullopt;
        else
            to_wstring(m::not_null(szstr), str);
    }

    void
    to_wstring(m::not_null<m::czstring> szstr, std::wstring& str)
    {
        to_wstring(std::string_view(szstr), str);
    }

    std::optional<std::wstring>
    to_wstring(m::czstring szstr)
    {
        if (!szstr)
            return std::nullopt;

        return to_wstring(m::not_null(szstr));
    }

    std::wstring
    to_wstring(m::not_null<m::czstring> szstr)
    {
        return to_wstring(std::string_view(szstr));
    }

    void
    to_wstring(std::string_view v, std::wstring& str)
    {
        acp_to_wstring(v, str);
    }

    std::wstring
    to_wstring(std::string_view v)
    {
        return acp_to_wstring(v);
    }

    void
    to_wstring(std::optional<std::string_view> v, std::optional<std::wstring>& str)
    {
        if (v)
        {
            std::wstring t;
            acp_to_wstring(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    std::wstring
    to_wstring(std::string const& s)
    {
        return to_wstring(std::string_view{s});
    }

    void
    to_wstring(std::string const& s, std::wstring& str)
    {
        to_wstring(std::string_view{s}, str);
    }

    std::optional<std::wstring>
    to_wstring(std::optional<std::string_view> v)
    {
        if (v)
            return acp_to_wstring(v.value());

        return std::nullopt;
    }

    void
    to_string(std::wstring const& s, std::string& str)
    {
        to_string(std::wstring_view{s}, str);
    }

    std::string
    to_string(std::wstring const& s)
    {
        return to_string(std::wstring_view{s});
    }

    void
    to_string(std::optional<std::wstring_view> v, std::optional<std::string>& str)
    {
        if (v)
        {
            std::string t;
            to_string(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    std::optional<std::string>
    to_string(std::optional<std::wstring_view> v)
    {
        if (v)
            return to_string(v.value());

        return std::nullopt;
    }

} // namespace m
