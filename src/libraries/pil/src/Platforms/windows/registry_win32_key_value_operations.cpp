// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

//
//

#include "pcwstr.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::registry::win32
{

    ikey::delete_value_disposition
    key::delete_value(delete_value_flags flags, std::u16string_view value_name)
    {
        M_API_PARAMETER_CHECK("ikey::delete_value", flags, {});

        auto const value_namez = pcwstr(value_name);
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
        M_API_PARAMETER_CHECK("ikey::enumerate_value_names_and_types", flags, {});

        std::size_t span_index{};

        while (span_index < values_span.size())
        {
            bool  end_of_values{false};
            DWORD dw_index = m::to<DWORD>(index);

            std::size_t size_to_try{64}; // somewhat arbitrary

            for (;;)
            {
                // give a short name to the current span element by binding a ref
                auto& value = values_span[span_index];
                bool  string_size_correct{false};

                value.m_value_name.resize_and_overwrite(
                    size_to_try, [&](char16_t* p, std::size_t len) -> std::size_t {
                        DWORD dw_cch_value_name{m::to<DWORD>(len)};
                        auto  status =
                            ::RegEnumValueW(m_hkey,
                                            dw_index,
                                            reinterpret_cast<PWSTR>(p),
                                            &dw_cch_value_name,
                                            nullptr,
                                            reinterpret_cast<DWORD*>(&value.m_reg_value_type),
                                            nullptr,
                                            nullptr);

                        if (status == ERROR_MORE_DATA)
                        {
                            size_to_try = dw_cch_value_name;
                            return 0;
                        }

                        if (status == ERROR_NO_MORE_ITEMS)
                        {
                            end_of_values = true;
                            return 0;
                        }

                        if (status != ERROR_SUCCESS)
                            m::throw_win32_error_code(status);

                        string_size_correct = true;
                        return dw_cch_value_name;
                    });

                if (end_of_values)
                    break;

                if (string_size_correct)
                    break;
            }

            if (end_of_values)
                break;

            index++;
            span_index++;
        }

        values_span = values_span.subspan(0, span_index);

        return enumerate_value_names_and_types_disposition{};
    }

    ikey::get_value_size_disposition
    key::get_value_size(get_value_size_flags flags,
                        std::u16string_view  value_name,
                        std::size_t&         size)
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

    ikey::get_value_type_disposition
    key::get_value_type(get_value_type_flags flags,
                        std::u16string_view  value_name,
                        reg_value_type&      type)
    {
        type = reg_value_type{};

        if (flags != get_value_type_flags{})
            throw std::runtime_error("Invalid flags to key::get_value_type() call");

        auto const value_namez = pcwstr(value_name);

        auto status = ::RegQueryValueExW(
            m_hkey, value_namez, nullptr, reinterpret_cast<DWORD*>(&type), nullptr, nullptr);

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return get_value_type_disposition{};
    }

    ikey::get_value_disposition
    key::get_value(get_value_flags             flags,
                   std::u16string_view         value_name,
                   reg_value_type&             type,
                   std::span<std::byte>&       value,
                   std::optional<std::size_t>& new_bytes_required)
    {
        new_bytes_required = std::nullopt;
        type               = reg_value_type{};

        if (flags != get_value_flags{})
            throw std::runtime_error("Invalid flags to key::get_value() call");

        auto const     value_namez = pcwstr(value_name);
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
    key::set_value(set_value_flags            flags,
                   std::u16string_view        value_name,
                   reg_value_type             type,
                   std::span<std::byte const> value)
    {
        if (flags != set_value_flags{})
            throw std::runtime_error("Invalid flags to key::set_value() call");

        auto const value_namez   = pcwstr(value_name);
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

} // namespace m::pil::impl::registry::win32
