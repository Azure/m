// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <ios>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

#include <gsl/gsl>

#include <Windows.h>

namespace m
{
    namespace windows
    {
        //
        // a/k/a "The type returned from GetLastError()"
        // 
        // The classic DOS error codes which are what the "true"
        // Win32 APIs return are all in the range [1 .. 65535] and
        // 0 == ERROR_SUCCESS which is the sole indication of success.
        // 
        // However, the type is defined in the windows headers as a
        // DWORD which is an unsigned 32 bit number and over time,
        // additional values have come to be stored in the last error
        // field in the TEB (Thread Environment Block). Notably, it's
        // not entirely uncommon to find an HRESULT in the last error
        // value.
        //
        enum class win32_error_code : uint32_t
        {
            success = ERROR_SUCCESS,
        };

        constexpr bool
        is_success(win32_error_code ec)
        {
            return ec == win32_error_code::success;
        }

        // Type-safe version of Win32 ::GetLastError()
        win32_error_code
        get_last_error();
    } // namespace windows
} // namespace m