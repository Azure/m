// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <ios>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

#include <Windows.h>

namespace m
{
    enum class hresult : int32_t
    {
        success = S_OK,
    };

    constexpr bool
    is_success(hresult hr)
    {
        return hr == hresult::success;
    }

    // Type-safe version of Win32 HRESULT_FROM_WIN32(::GetLastError())
    hresult
    get_last_error_as_hresult();
} // namespace m