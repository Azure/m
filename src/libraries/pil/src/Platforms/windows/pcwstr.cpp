// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
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
    pcwstr::pcwstr(std::wstring_view v): m_value(v.begin(), v.end()) { m_value.append(L"\0"sv); }

    pcwstr::pcwstr(std::optional<std::wstring_view> v)
    {
        if (v.has_value())
        {
            auto wsv   = v.value();
            auto outit = std::back_inserter(m_value);
            outit      = std::copy(wsv.begin(), wsv.end(), outit);
            *outit++   = L'\0';
        }
    }

    pcwstr::pcwstr(std::u16string_view v)
    {
        auto outit = std::back_inserter(m_value);

        outit = std::transform(v.begin(), v.end(), outit, [](char16_t ch) -> wchar_t {
            return static_cast<wchar_t>(ch);
        });

        *outit++ = L'\0';
    }

    pcwstr::pcwstr(std::optional<std::u16string_view> v)
    {
        if (v.has_value())
        {
            auto usv   = v.value();
            auto outit = std::back_inserter(m_value);

            outit = std::transform(usv.begin(), usv.end(), outit, [](char16_t ch) -> wchar_t {
                return static_cast<wchar_t>(ch);
            });

            *outit++ = L'\0';
        }
    }

    pcwstr::pcwstr(pcwstr const& other): m_value(other.m_value) {}

    pcwstr::pcwstr(pcwstr&& other) noexcept
    {
        using std::swap;
        swap(m_value, other.m_value);
    }

    pcwstr&
    pcwstr::operator=(pcwstr const& other)
    {
        m_value = other.m_value;
        return *this;
    }

    pcwstr&
    pcwstr::operator=(pcwstr&& other) noexcept
    {
        using std::swap;
        swap(m_value, other.m_value);
        return *this;
    }

    constexpr pcwstr::operator wchar_t const*() const noexcept
    {
        return (m_value.size() > 0) ? m_value.data() : nullptr;
    }

} // namespace m::pil::impl
