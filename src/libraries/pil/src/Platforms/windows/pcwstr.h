// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace m::pil::impl
{
    class pcwstr
    {
    public:
        pcwstr() = default;
        pcwstr(std::wstring_view v);
        pcwstr(std::u16string_view v);
        pcwstr(std::optional<std::wstring_view> v);
        pcwstr(std::optional<std::u16string_view> v);
        pcwstr(pcwstr&& other) noexcept;
        pcwstr(pcwstr const& other);
        ~pcwstr() = default;

        pcwstr&
        operator=(pcwstr const& other);
        pcwstr&
        operator=(pcwstr&& other) noexcept;

        friend void
        swap(pcwstr& l, pcwstr& r) noexcept
        {
            using std::swap;

            swap(l.m_value, r.m_value);
        }

        constexpr
        operator wchar_t const*() const noexcept;

    private:
        std::wstring m_value;
    };

} // namespace m::pil::impl
