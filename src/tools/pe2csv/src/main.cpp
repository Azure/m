// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <m/byte_streams/byte_streams.h>
#include <m/command_options/command_options.h>
#include <m/csv/writer.h>
#include <m/filesystem/filesystem.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

int
main(int argc, char const* argv[])
{

    for (int i = 1; i < argc; i++)
    {
        auto path       = m::filesystem::make_path(argv[i]);
        auto file       = m::filesystem::open_seekable_input_file(path);
        auto decoded_pe = m::pe::decoder(file);

        std::wstring buffer;
        auto         iter = std::back_inserter(buffer);

        auto path_name      = path.filename();
        auto path_name_str  = m::to_wstring(path_name.c_str());
        auto path_name_view = std::wstring_view(path_name_str);

        auto csv_writer = m::csv::writer(iter);

        for (auto&& imp: decoded_pe.m_image_import_descriptors)
        {
            auto import_name_sv = std::wstring_view(imp.m_name_string);

            for (auto&& f: imp.m_import_name_table_entries)
            {
                switch (f.index())
                {
                    case m::pe::image_import_descriptor::k_import_name_table_entry_type_index_name:
                    {
                        //
                        auto import_name =
                            std::wstring_view(std::get<m::pe::image_import_descriptor::
                                         k_import_name_table_entry_type_index_name>(f).m_name_string);

                        auto cols = {L"IMPORT"sv,
                                     path_name_view,
                                     import_name_sv,
                                     import_name};

                        csv_writer.write_row(cols);

                        break;
                    }

                    case m::pe::image_import_descriptor::
                        k_import_name_table_entry_type_index_ordinal:
                    {
                        std::wstring ordinal_buffer;
                        auto         buffer_iter = std::back_inserter(ordinal_buffer);

                        auto import_ordinal =
                            std::get<m::pe::image_import_descriptor::
                                         k_import_name_table_entry_type_index_ordinal>(f);

                        std::format_to(buffer_iter, L"#{:#x}", import_ordinal);

                        auto ordinal_view = std::wstring_view(ordinal_buffer);

                        auto cols = {L"IMPORT"sv, path_name_view, import_name_sv, ordinal_view};

                        csv_writer.write_row(cols);

                        break;
                    }
                }
            }
        }

        csv_writer.write_end();

        std::wcout << buffer;
    }

    return EXIT_SUCCESS;
}
