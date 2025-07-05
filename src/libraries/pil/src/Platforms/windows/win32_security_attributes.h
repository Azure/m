// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/pil/security_attributes.h>

namespace m::pil::impl
{
    class win32_security_attributes
    {
    public:
        win32_security_attributes();
        win32_security_attributes(security_attributes v);
        win32_security_attributes(std::optional<security_attributes> v);
        win32_security_attributes(win32_security_attributes const& other);
        ~win32_security_attributes() = default;

        win32_security_attributes&
        operator=(win32_security_attributes const& other);

        friend void
        swap(win32_security_attributes& l, win32_security_attributes& r) noexcept
        {
            using std::swap;

            swap(l.m_sa.bInheritHandle, r.m_sa.bInheritHandle);
            swap(l.m_sa.lpSecurityDescriptor, r.m_sa.lpSecurityDescriptor);
            swap(l.m_sa.nLength, r.m_sa.nLength);
        }

        constexpr
        operator SECURITY_ATTRIBUTES const*() const noexcept;

        constexpr
        operator SECURITY_ATTRIBUTES*() noexcept;

    private:
        SECURITY_ATTRIBUTES m_sa;
    };

} // namespace m::pil::impl
