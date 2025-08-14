// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/errors/errors.h>
#include <m/errors/hresult.h>
#include <m/exception/exception.h>
#include <m/utility/to_underlying.h>
#include <m/utility/zstring.h>

#include <Windows.h>

inline const std::error_category&
m::hresult_category() noexcept
{
    return m::windows_details::hresult_category_instance;
}

namespace
{
    void
    try_throw_native_m_exception(HRESULT hr, m::zstring what = nullptr)
    {
        switch (hr)
        {
            case HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND): throw m::not_found(what);
            case HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION): throw m::sharing_violation(what);
        }
    }
} // namespace

void
m::throw_hresult(HRESULT hr)
{
    try_throw_native_m_exception(hr);
    m::dbg_format(L"About to throw unmapped HRESULT {:#x}", static_cast<ULONG>(hr));
    throw std::system_error(hr, m::hresult_category());
}

void
m::throw_hresult(HRESULT hr, m::zstring what)
{
    try_throw_native_m_exception(hr, what);
    m::dbg_format(L"About to throw unmapped HRESULT {:#x}", static_cast<ULONG>(hr));
    throw std::system_error(hr, m::hresult_category(), what);
}

void
m::throw_win32_error_code(DWORD error_code)
{
    throw_hresult(HRESULT_FROM_WIN32(error_code));
}

void
m::throw_win32_error_code(DWORD error_code, m::zstring what)
{
    throw_hresult(HRESULT_FROM_WIN32(error_code), what);
}

void
m::throw_error(windows::win32_error_code error_code)
{
    throw_win32_error_code(to_underlying(error_code));
}

void
m::throw_error(windows::win32_error_code error_code, m::zstring what)
{
    throw_win32_error_code(to_underlying(error_code), what);
}

void
m::throw_last_win32_error()
{
    auto const last_error = ::GetLastError();
    throw_hresult(HRESULT_FROM_WIN32(last_error));
}

bool
m::failed(windows::win32_error_code ec)
{
    return ec != windows::win32_error_code::success;
}

void
m::throw_if_failed(windows::win32_error_code ec)
{
    if (failed(ec))
        throw_error(ec);
}

std::error_code
m::get_last_win32_error()
{
    return std::error_code(static_cast<int>(get_last_error_as_hresult()), m::hresult_category());
}

std::error_code
m::make_win32_error_code(DWORD win32_error_code)
{
    return std::error_code(static_cast<int>(HRESULT_FROM_WIN32(win32_error_code)),
                           m::hresult_category());
}

std::error_code
m::make_hresult_error_code(HRESULT hr)
{
    return std::error_code(static_cast<int>(hr), m::hresult_category());
}
