// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>

#include <m/errors/win32_error_code.h>

#include <Windows.h>

m::windows::win32_error_code
m::windows::get_last_error()
{
    return win32_error_code{::GetLastError()};
}
