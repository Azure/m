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
#include <m/pe/loader.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

using namespace std::string_view_literals;

#include "known_dlls.h"
#include "traverse.h"

std::wstring
downcase(std::wstring_view v)
{
    std::wstring result;
    auto         it = std::back_inserter(result);

    std::ranges::for_each(v, [&](wchar_t wch) {
        if (wch <= (std::numeric_limits<unsigned char>::max)())
        {
            *it++ = static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(wch)));
        }
        else
        {
            *it++ = wch;
        }
    });

    return result;
}

void
m::pe2l::traverse(std::filesystem::path const& path)
{
    auto parent_path = path.parent_path();

    auto known_dll_resolver =
        [](std::filesystem::path const& name) -> m::pe::loader::path_resolver_result {
        static std::set<std::wstring> known_dlls(known_apiset_dlls.begin(),
                                                 known_apiset_dlls.end());

        if (known_dlls.contains(m::to_wstring(name.native())))
            return m::pe::loader::trim_graph;

        return m::pe::loader::pe_not_found;
    };

    auto regular_dll_resolver =
        [&parent_path](std::filesystem::path const& name) -> m::pe::loader::path_resolver_result {
        std::filesystem::path resolved_path{};

        if (name.is_absolute())
            resolved_path = name;
        else
            resolved_path = parent_path / name;

        if (std::filesystem::exists(resolved_path))
        {
            resolved_path = resolved_path.lexically_normal();
            return resolved_path;
        }

        return m::pe::loader::pe_not_found;
    };

    auto loader = m::pe::loader::loader({ known_dll_resolver, regular_dll_resolver });

    loader.resolve(path);

    if (loader.unresolved_count() == 0)
    {
        std::wcout << std::format(L"All imports of {} successfully resolved.\n",
                                  m::to_wstring(path.c_str()));
    }
    else
    {
        std::wcout << std::format(L"{} imported DLLs of {} not found\n\n",
                                  loader.unresolved_count(),
                                  m::to_wstring(path.c_str()));

        loader.for_each_not_found([](auto n, auto begin, auto end) {
            std::wcout << n << "\n";
            auto it = begin;
            while (it != end)
            {
                std::wcout << "   [from " << *it++ << "]\n";
            }
        });
    }
}
