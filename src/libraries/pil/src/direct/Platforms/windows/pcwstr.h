// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <m/pil/registry_base_types.h>
#include <m/pil/key_path.h>

namespace m::pil::impl
{
    class pcwstr
    {
    public:
        pcwstr() = default;
        pcwstr(std::wstring_view v);
        pcwstr(std::wstring const& v);
        pcwstr(std::u16string_view v);

        pcwstr(std::optional<pil::key_path> const& v);
        pcwstr(pil::key_path const& v);

        pcwstr(std::optional<std::wstring_view> v);
        pcwstr(std::optional<std::u16string_view> v);
        pcwstr(pcwstr&& other) noexcept;
        pcwstr(pcwstr const& other);
        pcwstr(char16_t const* ptr) noexcept;
        pcwstr(wchar_t const* ptr) noexcept;
        ~pcwstr() = default;

        pcwstr&
        operator=(pcwstr const& other);
        pcwstr&
        operator=(pcwstr&& other) noexcept;

        void
        swap(pcwstr& other) noexcept
        {
            using std::swap;

            swap(m_value, other.m_value);
            swap(m_c_str, other.m_c_str);
        }

        constexpr
        operator wchar_t const*() const noexcept
        {
            return m_c_str;
        }

    private:
        wchar_t const* m_c_str{};
        std::wstring   m_value;
    };

} // namespace m::pil::impl
