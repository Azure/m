// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <string>
#include <string_view>

#include <m/cast/cast.h>
#include <m/filesystem/filesystem.h>
#include <m/strings/convert.h>

std::filesystem::path
m::filesystem::make_path(std::string_view v)
{
#ifdef WIN32
    return std::filesystem::path(m::to_wstring(v));
#else
    return std::filesystem::path(v);
#endif
}

std::filesystem::path
m::filesystem::make_path(std::wstring_view v)
{
#ifdef WIN32
    return std::filesystem::path(v);
#else
    return std::filesystem::path(m::to_string(v));
#endif
}

