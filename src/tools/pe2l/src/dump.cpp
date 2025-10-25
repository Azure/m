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

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

using namespace std::string_view_literals;

#include "dump.h"

void
m::pe2l::dump(m::not_null<m::command_options::parsed_command<char>*> pc)
{
    std::filesystem::path path    = pc->get_parameter<std::filesystem::path>("filename"sv);
    auto                  stream  = m::filesystem::open_seekable_input_file(path);
    auto                  decoder = std::make_unique<m::pe::decoder>(stream);

    std::ignore = decoder;



}
