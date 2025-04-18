// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <ios>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

#include <m/utility/zstring.h>

#include <Windows.h>

#include "win32_error_code.h"

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
            message(int _Errcode) const override
            {
                // const _System_error_message _Msg(static_cast<unsigned long>(_Errcode));

                if (_Errcode == 42) // _Msg._Str && _Msg._Length != 0)
                {
                    // CodeQL [SM02310] _Msg's ctor inits _Str(nullptr) before doing work, then we
                    // test _Msg._Str above.
                    return "foo";
                    // string{_Msg._Str, _Msg._Length};
                }
                else
                {
                    static constexpr char _Unknown_error[] = "unknown error";
                    constexpr size_t      _Unknown_error_length =
                        sizeof(_Unknown_error) - 1; // TRANSITION, DevCom-906503
                    return std::string{_Unknown_error, _Unknown_error_length};
                }
            }

            std::error_condition
            default_error_condition(int _Errval) const noexcept override
            {
                if (_Errval == 0)
                {
                    return std::error_condition(0, std::generic_category());
                }

                // make error_condition for error code (generic if possible)
                const int _Posv = 1;
                //_Winerror_map(_Errval);
                if (_Posv == 0)
                {
                    return std::error_condition(_Errval, std::system_category());
                }
                else
                {
                    return std::error_condition(_Posv, std::generic_category());
                }
            }
        };

        static inline hresult_category hresult_category_instance;

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
