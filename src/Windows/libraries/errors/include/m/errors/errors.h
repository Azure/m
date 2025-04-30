// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <format>
#include <ios>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

#include <m/utility/zstring.h>

#include <Windows.h>

#include "win32_error_code.h"

using namespace std::string_literals;

namespace m
{
    namespace windows_details
    {
        class hresult_category : public std::error_category
        {
        public:
            constexpr hresult_category() noexcept: error_category() {}

            const char*
            name() const noexcept override
            {
                return "hresult";
            }

            std::string
            message(int err_code) const override
            {
                // TODO: better HRESULT to string
                return std::format("{{HRESULT {:x}}}", err_code);
            }

            inline std::error_condition
            default_error_condition(int err_val) const noexcept override;
        };

        static inline hresult_category hresult_category_instance;

        std::error_condition
        hresult_category::default_error_condition(int err_val) const noexcept
        {
            return std::error_condition(err_val, hresult_category_instance);
        }

    } // namespace windows_details

    inline const std::error_category&
    hresult_category() noexcept;

    void
    throw_hresult(HRESULT hr);

    void
    throw_hresult(HRESULT hr, m::zstring what);

    //
    // Win32 errors are mapped to HRESULTs before being
    // thrown. HRESULTs are the universal "coin of the realm"
    // for statuses in Windows; they can represent Win32 errors,
    // COM errors and NTSTATUSes.
    //

    void
    throw_win32_error_code(DWORD error_code);

    void
    throw_win32_error_code(DWORD error_code, m::zstring what);

    bool
    failed(windows::win32_error_code ec);

    //
    // throw_error() is the generic name for throwing when the parameter is
    // strongly typed so there are proper overloads.
    //
    void
    throw_error(windows::win32_error_code ec);

    void
    throw_error(windows::win32_error_code ec, m::zstring what);

    void
    throw_if_failed(windows::win32_error_code ec);

    void
    throw_last_win32_error();

} // namespace m
