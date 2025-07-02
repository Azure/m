// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <string>
#include <string_view>

#include <m/errors/errors.h>
#include <m/strings/convert.h>

//
//

#include "win32_security_attributes.h"

using namespace std::string_view_literals;

namespace m::pil::impl
{
    win32_security_attributes::win32_security_attributes()
    {
        m_sa.bInheritHandle       = FALSE;
        m_sa.lpSecurityDescriptor = nullptr;
        m_sa.nLength              = 0;
    }

    win32_security_attributes::win32_security_attributes(security_attributes v)
    {
        m_sa.nLength              = sizeof(m_sa);
        m_sa.bInheritHandle       = v.m_inherit_handle ? TRUE : FALSE;
        m_sa.lpSecurityDescriptor = v.m_security_descriptor;
    }

    win32_security_attributes::win32_security_attributes(std::optional<security_attributes> v)
    {
        if (v.has_value())
        {
            m_sa.nLength              = sizeof(m_sa);
            m_sa.bInheritHandle       = v.value().m_inherit_handle ? TRUE : FALSE;
            m_sa.lpSecurityDescriptor = v.value().m_security_descriptor;
        }
        else
        {
            m_sa.bInheritHandle       = FALSE;
            m_sa.lpSecurityDescriptor = nullptr;
            m_sa.nLength              = 0;
        }
    }

    win32_security_attributes::win32_security_attributes(win32_security_attributes const& other):
        m_sa(other.m_sa)
    {}

    win32_security_attributes&
    win32_security_attributes::operator=(win32_security_attributes const& other)
    {
        m_sa = other.m_sa;
        return *this;
    }

    constexpr win32_security_attributes::operator SECURITY_ATTRIBUTES const*() const noexcept
    {
        return (m_sa.nLength > 0) ? &m_sa : nullptr;
    }

    constexpr win32_security_attributes::operator SECURITY_ATTRIBUTES*() noexcept
    {
        return (m_sa.nLength > 0) ? &m_sa : nullptr;
    }

} // namespace m::pil::impl
