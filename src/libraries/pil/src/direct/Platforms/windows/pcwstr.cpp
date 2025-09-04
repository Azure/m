// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <m/errors/errors.h>
#include <m/strings/convert.h>

//
//

#include "pcwstr.h"

using namespace std::string_view_literals;

namespace m::pil::impl
{
    //
    pcwstr::pcwstr(std::wstring_view v): m_value(v.begin(), v.end())
    {
        m_value.append(L"\0"sv);
        m_c_str = m_value.data();
    }

    pcwstr::pcwstr(std::wstring const& v): m_value(v.begin(), v.end())
    {
        m_value.append(L"\0"sv);
        m_c_str = m_value.data();
    }

    pcwstr::pcwstr(std::optional<pil::key_path> const& v)
    {
        if (v.has_value())
        {
            auto        pv     = v.value();
            auto        outit  = std::back_inserter(m_value);
            auto const& native = pv.native();
            auto const  view   = native.view();
            outit              = std::copy(view.begin(), view.end(), outit);
            *outit++           = L'\0';
            m_c_str            = m_value.data();
        }
    }

    pcwstr::pcwstr(pil::key_path const& pv)
    {
        auto        outit  = std::back_inserter(m_value);
        auto const& native = pv.native();
        auto const  view   = native.view();
        outit              = std::copy(view.begin(), view.end(), outit);
        *outit++           = L'\0';
        m_c_str            = m_value.data();
    }

    pcwstr::pcwstr(std::optional<std::wstring_view> v)
    {
        if (v.has_value())
        {
            auto const& wsv   = v.value();
            auto        outit = std::back_inserter(m_value);
            outit             = std::copy(wsv.begin(), wsv.end(), outit);
            *outit++          = L'\0';
            m_c_str           = m_value.data();
        }
    }

    pcwstr::pcwstr(std::u16string_view v)
    {
        auto outit = std::back_inserter(m_value);

        outit = std::transform(v.begin(), v.end(), outit, [](char16_t ch) -> wchar_t {
            return static_cast<wchar_t>(ch);
        });

        *outit++ = L'\0';
        m_c_str  = m_value.data();
    }

    pcwstr::pcwstr(std::optional<std::u16string_view> v)
    {
        if (v.has_value())
        {
            auto const& usv   = v.value();
            auto        outit = std::back_inserter(m_value);

            outit = std::transform(usv.begin(), usv.end(), outit, [](char16_t ch) -> wchar_t {
                return static_cast<wchar_t>(ch);
            });

            *outit++ = L'\0';
            m_c_str  = m_value.data();
        }
    }

    pcwstr::pcwstr(pcwstr const& other): m_value(other.m_value)
    {
        if (m_value.size() != 0)
            m_c_str = m_value.data();
        else
        {
            // If there wasn't data in the string, we copy the other `pcwstr` object's
            // string pointer because it could be a simple pointer.
            m_c_str = other.m_c_str;
        }
    }

    pcwstr::pcwstr(pcwstr&& other) noexcept
    {
        using std::swap;
        swap(m_value, other.m_value);
        swap(m_c_str, other.m_c_str);
    }

    pcwstr::pcwstr(char16_t const* ptr) noexcept
    {
        m_c_str = reinterpret_cast<wchar_t const*>(ptr);
    }

    pcwstr::pcwstr(wchar_t const* ptr) noexcept { m_c_str = ptr; }

    pcwstr&
    pcwstr::operator=(pcwstr const& other)
    {
        m_value = other.m_value;

        if (m_value.size() != 0)
            m_c_str = m_value.data();
        else
        {
            // If there wasn't data in the string, we copy the other `pcwstr` object's
            // string pointer because it could be a simple pointer.
            m_c_str = other.m_c_str;
        }

        return *this;
    }

    pcwstr&
    pcwstr::operator=(pcwstr&& other) noexcept
    {
        using std::swap;
        swap(m_value, other.m_value);
        swap(m_c_str, other.m_c_str);
        return *this;
    }

} // namespace m::pil::impl
