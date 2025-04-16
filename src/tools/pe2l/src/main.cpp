// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <gsl/gsl>

#include <m/byte_streams/byte_streams.h>
#include <m/command_options/command_options.h>
#include <m/csv/writer.h>
#include <m/filesystem/filesystem.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

#include "traverse.h"

using namespace std::string_view_literals;



int
main(int argc, char const* argv[])
{
    using verb_t = m::command_options::verb<char>;

    std::filesystem::path filename_path;

#if 0

    auto path_parameter = std::make_unique<m::command_options::path_parameter>(p, "filename"sv);
    std::array<std::unique_ptr<m::command_options::parameter<char>>, 1> parameters{
        path_parameter.release()};
    std::array<std::unique_ptr<m::command_options::option<char>>, 0>    options;
    auto                                                try_load_verb =
        std::make_unique<m::command_options::verb<char>>("try-loader"sv, gsl::span(parameters), gsl::span(options));
    std::array<std::unique_ptr<m::command_options::verb<char>>, 1> verbs{try_load_verb.release()};
    auto verb_set = m::command_options::command_verb_set<char>(gsl::span(verbs));
#endif

    auto commands = m::command_options::command_verb_set<char>();

    verb_t& traverse_verb = commands.add_verb("traverse");
    traverse_verb.add_path_parameter("filename", filename_path);

    try
    {
        commands.process_command_line(argc, argv);
    }
    catch (std::runtime_error const& e)
    {
        std::wcerr << "Error parsing command line: " << e.what() << "\n";

        std::string buffer;
        buffer = commands.usage(argv[0]);
        auto text = buffer.c_str();

        std::wcerr << text << '\n';

        exit(EXIT_FAILURE);
    }

    m::pe2l::traverse(filename_path);

    return EXIT_SUCCESS;
}
