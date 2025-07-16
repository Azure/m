// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <string_view>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/utility/utility.h>

#include "platform.h"

namespace m::strings::impl::ordinal_case_insensitive
{
    bool
    less(std::string_view const& l, std::string_view const& r);

    bool
    less(std::wstring_view const& l, std::wstring_view const& r)
    {
        auto const compare_result = ::CompareStringOrdinal(
            l.data(), m::to<int>(l.size()), r.data(), m::to<int>(r.size()), TRUE);
        if (compare_result == 0)
            m::throw_last_win32_error();

        if (compare_result == CSTR_LESS_THAN)
            return true;

        if (compare_result == CSTR_EQUAL || compare_result == CSTR_GREATER_THAN)
            return false;

        M_INTERNAL_ERROR_CHECK((compare_result == CSTR_LESS_THAN) ||
                               (compare_result == CSTR_EQUAL) ||
                               (compare_result == CSTR_GREATER_THAN));

        return false; // should be dead code
    }

    bool
    less(std::u8string_view const& l, std::u8string_view const& r);

    bool
    less(std::u16string_view const& l, std::u16string_view const& r)
    {
        auto const compare_result = ::CompareStringOrdinal(reinterpret_cast<PCWCH>(l.data()),
                                                           m::to<int>(l.size()),
                                                           reinterpret_cast<PCWCH>(r.data()),
                                                           m::to<int>(r.size()),
                                                           TRUE);
        if (compare_result == 0)
            m::throw_last_win32_error();

        if (compare_result == CSTR_LESS_THAN)
            return true;

        if (compare_result == CSTR_EQUAL || compare_result == CSTR_GREATER_THAN)
            return false;

        M_INTERNAL_ERROR_CHECK((compare_result == CSTR_LESS_THAN) ||
                               (compare_result == CSTR_EQUAL) ||
                               (compare_result == CSTR_GREATER_THAN));

        return false; // should be dead code
    }

    bool
    less(std::u32string_view const& l, std::u32string_view const& r);

    //
} // namespace m::strings::impl::ordinal_case_insensitive

