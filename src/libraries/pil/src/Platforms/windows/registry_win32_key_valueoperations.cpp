// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "pcwstr.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::registry::win32
{

    m::pil::registry::interfaces::key::delete_value_disposition
    key::delete_value(m::pil::registry::interfaces::key::delete_value_flags flags,
                      std::u16string_view                                   value_name)
    {
        if (flags != delete_value_flags{})
            throw std::runtime_error("Invalid flags to key::delete_value() call");

        auto const value_namez = pcwstr(value_name);
        auto       status      = ::RegDeleteValueW(m_hkey, value_namez);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return delete_value_disposition{};
    }

    m::pil::registry::interfaces::key::enumerate_values_disposition
    key::enumerate_values(
        m::pil::registry::interfaces::key::enumerate_values_flags                 flags,
        std::size_t                                                               index,
        std::u16string&                                                           value_name_buffer,
        std::optional<m::pil::registry::interfaces::key::enumerate_values_value>& value)
    {
        value = std::nullopt;

        if (flags != enumerate_values_flags{})
            throw std::runtime_error("Invalid flags to key::enumerate_values() call");

        pil::registry::interfaces::key::enumerate_values_value v;
        DWORD                                                  dw_index = m::to<DWORD>(index);

        // The done Boolean is used to indicate when we have reached the end of the enumeration
        bool        done{false};
        std::size_t size_to_try{64}; // somewhat arbitrary

        for (;;)
        {
            value_name_buffer.resize_and_overwrite(
                size_to_try,
                [&, hkey = m_hkey.get(), dw_index, p_type = reinterpret_cast<DWORD*>(&v.m_type)](
                    char16_t* p, std::size_t len) -> std::size_t {
                    DWORD dw_cch_value_name{m::to<DWORD>(len)};
                    auto  status = ::RegEnumValueW(hkey,
                                                  dw_index,
                                                  reinterpret_cast<PWSTR>(p),
                                                  &dw_cch_value_name,
                                                  nullptr,
                                                  p_type,
                                                  nullptr,
                                                  nullptr);

                    if (status == ERROR_MORE_DATA)
                        return 0;

                    if (status == ERROR_NO_MORE_ITEMS)
                    {
                        done = true;
                        return 0;
                    }

                    if (status != ERROR_SUCCESS)
                        m::throw_win32_error_code(status);

                    return dw_cch_value_name;
                });

            if (done)
                return enumerate_values_disposition{};

            // If the string ends up smaller than the buffer size requested,
            // we are done the loop.
            if (value_name_buffer.size() != size_to_try)
                break;
        }

        v.m_value_name = std::u16string_view(value_name_buffer.begin(), value_name_buffer.end());
        value          = v;

        return enumerate_values_disposition{};
    }

    m::pil::registry::interfaces::key::get_value_size_disposition
    key::get_value_size(m::pil::registry::interfaces::key::get_value_size_flags flags,
                        std::u16string_view                                     value_name,
                        std::size_t&                                            size)
    {
        size = 0;

        if (flags != get_value_size_flags{})
            throw std::runtime_error("Invalid flags to key::get_value_size() call");

        auto const value_namez = pcwstr(value_name);

        DWORD dw_cb_data{};
        auto  status =
            ::RegQueryValueExW(m_hkey, value_namez, nullptr, nullptr, nullptr, &dw_cb_data);

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        size = dw_cb_data;

        return get_value_size_disposition{};
    }

    m::pil::registry::interfaces::key::get_value_type_disposition
    key::get_value_type(m::pil::registry::interfaces::key::get_value_type_flags flags,
                        std::u16string_view                                     value_name,
                        pil::registry::value_type&                              type)
    {
        type = pil::registry::value_type{};

        if (flags != get_value_type_flags{})
            throw std::runtime_error("Invalid flags to key::get_value_type() call");

        auto const value_namez = pcwstr(value_name);

        auto status = ::RegQueryValueExW(
            m_hkey, value_namez, nullptr, reinterpret_cast<DWORD*>(&type), nullptr, nullptr);

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return get_value_type_disposition{};
    }

    m::pil::registry::interfaces::key::get_value_disposition
    key::get_value(m::pil::registry::interfaces::key::get_value_flags flags,
                   std::u16string_view                                value_name,
                   pil::registry::value_type&                         type,
                   std::span<std::byte>&                              value,
                   std::optional<std::size_t>&                        new_bytes_required)
    {
        new_bytes_required = std::nullopt;
        type               = pil::registry::value_type{};

        if (flags != get_value_flags{})
            throw std::runtime_error("Invalid flags to key::get_value() call");

        auto const                value_namez = pcwstr(value_name);
        pil::registry::value_type vt{};

        DWORD dw_cb_data{m::to<DWORD>(value.size())};
        auto  status = ::RegQueryValueExW(m_hkey,
                                         value_namez,
                                         nullptr,
                                         reinterpret_cast<DWORD*>(&vt),
                                         reinterpret_cast<LPBYTE>(value.data()),
                                         &dw_cb_data);

        if (status == ERROR_MORE_DATA)
        {
            new_bytes_required = dw_cb_data;
            return get_value_disposition{};
        }

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        // Trim the span down to what we actually populated
        value = value.subspan(0, dw_cb_data);
        type  = vt;

        return get_value_disposition{};
    }

    m::pil::registry::interfaces::key::set_value_disposition
    key::set_value(m::pil::registry::interfaces::key::set_value_flags flags,
                   std::u16string_view                                value_name,
                   pil::registry::value_type                          type,
                   std::span<std::byte const>                         value)
    {
        if (flags != set_value_flags{})
            throw std::runtime_error("Invalid flags to key::set_value() call");

        auto const value_namez = pcwstr(value_name);
        DWORD      dw_value_type = static_cast<DWORD>(type);
        DWORD      dw_cb_data{m::to<DWORD>(value.size())};

        auto status =
            ::RegSetValueExW(m_hkey, value_namez, 0, dw_value_type, reinterpret_cast<BYTE const*>(value.data()), dw_cb_data);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return set_value_disposition{};
    }

} // namespace m::pil::impl::registry::win32
