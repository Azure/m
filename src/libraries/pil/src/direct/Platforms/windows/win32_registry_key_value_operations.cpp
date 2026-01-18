// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

#include "pcwstr.h"
#include "win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::win32
{

    ikey::delete_value_disposition
    key::delete_value(delete_value_flags flags, value_name_string_type const& value_name)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_value", flags);

        auto const value_namez = pcwstr(value_name.c_str());
        auto       status      = ::RegDeleteValueW(m_hkey, value_namez);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return delete_value_disposition{};
    }

    ikey::enumerate_value_names_and_types_disposition
    key::enumerate_value_names_and_types(
        enumerate_value_names_and_types_flags                                  flags,
        std::size_t                                                            index,
        std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::enumerate_value_names_and_types", flags);

        std::size_t span_index{};

        while (span_index < values_span.size())
        {
            // give a short name to the current span element by binding a ref
            auto& value = values_span[span_index];

            bool  end_of_values{false};
            DWORD dw_index = m::to<DWORD>(index);

            constexpr DWORD initial_size = 64;

            DWORD                 size_to_try{initial_size}; // somewhat arbitrary
            std::vector<char16_t> buffer(initial_size, u'\0');

            M_INTERNAL_ERROR_CHECK(size_to_try == buffer.size());

            for (;;)
            {
                DWORD current_size = size_to_try;

                buffer[buffer.size() - 2] = u'\0';

                auto status =
                    ::RegEnumValueW(m_hkey,                                 // hkey
                                    dw_index,                               // dwIndex
                                    reinterpret_cast<PWSTR>(buffer.data()), // lpValueName
                                    &current_size,                          // lpcchValueName
                                    nullptr,                                // lpReserved
                                    reinterpret_cast<DWORD*>(&value.m_reg_value_type), // lpType
                                    nullptr,                                           // lpData
                                    nullptr);                                          // lpcbData
                if (status == ERROR_NO_MORE_ITEMS)
                {
                    end_of_values = true;
                    break;
                }

                if (status == ERROR_MORE_DATA)
                {
                    size_to_try = size_to_try + (size_to_try / 2);

                    if (current_size >= size_to_try)
                        current_size = m::math::add(current_size, 2, current_size);

                    buffer.resize(size_to_try);

                    continue;
                }

                if (status != ERROR_SUCCESS)
                    m::throw_win32_error_code(status);

                if (buffer[buffer.size() - 2] == u'\0')
                {
                    size_to_try = current_size;
                    break;
                }

                size_to_try = size_to_try + (size_to_try / 2);
                buffer.resize(size_to_try);
            }

            if (end_of_values)
                break;

            value.m_value_name = std::u16string_view(buffer.data(), size_to_try);

            index++;
            span_index++;
        }

        values_span = values_span.subspan(0, span_index);

        return enumerate_value_names_and_types_disposition{};
    }

    ikey::get_value_size_disposition
    key::get_value_size(get_value_size_flags          flags,
                        value_name_string_type const& value_name,
                        std::size_t&                  size)
    {
        size = 0;

        if (flags != get_value_size_flags{})
            throw std::runtime_error("Invalid flags to key::get_value_size() call");

        auto const value_namez = pcwstr(value_name.c_str());

        DWORD dw_cb_data{};
        auto  status =
            ::RegQueryValueExW(m_hkey, value_namez, nullptr, nullptr, nullptr, &dw_cb_data);

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        size = dw_cb_data;

        return get_value_size_disposition{};
    }

    ikey::get_value_type_disposition
    key::get_value_type(get_value_type_flags          flags,
                        value_name_string_type const& value_name,
                        reg_value_type&               type)
    {
        type = reg_value_type{};

        if (flags != get_value_type_flags{})
            throw std::runtime_error("Invalid flags to key::get_value_type() call");

        auto const value_namez = pcwstr(value_name.c_str());

        auto status = ::RegQueryValueExW(
            m_hkey, value_namez, nullptr, reinterpret_cast<DWORD*>(&type), nullptr, nullptr);

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return get_value_type_disposition{};
    }

    ikey::get_value_disposition
    key::get_value(get_value_flags               flags,
                   value_name_string_type const& value_name,
                   reg_value_type&               type,
                   std::span<std::byte>&         value,
                   std::optional<std::size_t>&   new_bytes_required)
    {
        new_bytes_required = std::nullopt;
        type               = reg_value_type{};

        if (flags != get_value_flags{})
            throw std::runtime_error("Invalid flags to key::get_value() call");

        auto const     value_namez = pcwstr(value_name.c_str());
        reg_value_type vt{};

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

    ikey::set_value_disposition
    key::set_value(set_value_flags               flags,
                   value_name_string_type const& value_name,
                   reg_value_type                type,
                   std::span<std::byte const>    value)
    {
        if (flags != set_value_flags{})
            throw std::runtime_error("Invalid flags to key::set_value() call");

        auto const value_namez   = pcwstr(value_name.c_str());
        DWORD      dw_value_type = static_cast<DWORD>(type);
        DWORD      dw_cb_data{m::to<DWORD>(value.size())};

        auto status = ::RegSetValueExW(m_hkey,
                                       value_namez,
                                       0,
                                       dw_value_type,
                                       reinterpret_cast<BYTE const*>(value.data()),
                                       dw_cb_data);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return set_value_disposition{};
    }

} // namespace m::pil::impl::win32
