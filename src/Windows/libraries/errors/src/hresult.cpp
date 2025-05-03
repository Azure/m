// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>

#include <m/errors/hresult.h>

#include <Windows.h>

m::hresult
m::get_last_error_as_hresult()
{
    return hresult{HRESULT_FROM_WIN32(::GetLastError())};
}
