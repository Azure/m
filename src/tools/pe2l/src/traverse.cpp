// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <queue>
#include <ranges>
#include <set>
#include <string>
#include <string_view>

#include <m/byte_streams/byte_streams.h>
#include <m/command_options/command_options.h>
#include <m/csv/writer.h>
#include <m/filesystem/filesystem.h>
#include <m/pe/loader_context.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

using namespace std::string_view_literals;

#include "traverse.h"

std::wstring
downcase(std::wstring_view v)
{
    std::wstring result;
    auto         it = std::back_inserter(result);

    std::ranges::for_each(v, [&](wchar_t wch) {
        if (wch <= std::numeric_limits<unsigned char>::max())
        {
            *it = std::tolower(static_cast<unsigned char>(wch));
            ++it;
        }
        else
        {
            *it = wch;
            ++it;
        }
    });

    return result;
}

void
m::pe2l::traverse(std::filesystem::path const& path)
{
    auto loader = m::pe::loader_context();

    loader.resolve(path);

    if (loader.unresolved_count() == 0)
    {
        std::wcout << std::format(L"All imports of {} successfully resolved.\n", m::to_wstring(path.c_str()));
    }
    else
    {
        std::wcout << std::format(
            L"{} imported DLLs of {} not found\n\n", loader.unresolved_count(), m::to_wstring(path.c_str()));

        loader.for_each_not_found([](auto n) { std::wcout << n << "\n"; });
    }
}
