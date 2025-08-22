// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace m
{
    //
    // This would be superseded by the <print> header's contents but:
    //
    // 1. There is no support for a std::wprint()
    //
    // 2. Support for c++23 is inconsistent
    //

    template <typename... Args>
    void
    println(std::format_string<Args...> fmt, Args&&... args)
    {
        std::cout << std::vformat(fmt.get(), std::make_format_args(args...)) << std::endl;
    }

    template <typename... Args>
    void
    wprintln(std::wformat_string<Args...> fmt, Args&&... args)
    {
        std::wcout << std::vformat(fmt.get(), std::make_wformat_args(args...)) << std::endl;
    }

} // namespace m
