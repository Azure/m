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

#if WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

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

        auto path_name           = path.filename();
        auto path_name_str       = m::filesystem::path_to_wstring(path_name);
        auto utf8_path_name_str  = m::to_u8string(path_name_str);
        auto utf8_path_name_view = std::u8string_view(utf8_path_name_str);

        auto csv_writer = m::csv::writer([&](auto spn) { std::ranges::copy(spn, iter); });

        for (auto&& imp: decoded_pe.m_image_import_descriptors)
        {
            auto utf8_name    = m::to_u8string(imp.m_name_string);
            auto utf8_name_sv = std::u8string_view(utf8_name);

            for (auto&& f: imp.m_import_name_table_entries)
            {
                switch (f.index())
                {
                    case m::pe::image_import_descriptor::k_import_name_table_entry_type_index_name:
                    {
                        auto utf8_import_name = m::to_u8string(
                            std::get<m::pe::image_import_descriptor::
                                         k_import_name_table_entry_type_index_name>(f)
                                .m_name_string);
                        auto utf8_import_name_sv = std::u8string_view(utf8_import_name);

                        auto cols = {
                            u8"IMPORT"sv, utf8_path_name_view, utf8_name_sv, utf8_import_name_sv};

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

                        auto       ordinal_view      = std::wstring_view(ordinal_buffer);
                        auto       utf8_ordinal      = m::to_u8string(ordinal_view);
                        auto const utf8_ordinal_view = std::u8string_view(utf8_ordinal);

                        auto cols = {
                            u8"IMPORT"sv, utf8_path_name_view, utf8_name_sv, utf8_ordinal_view};

                        csv_writer.write_row(cols);

                        break;
                    }
                }
            }
        }

        for (auto const& e: decoded_pe.m_image_export_directory.m_names)
        {
            auto const utf8_export_name      = m::to_u8string(e.c_str());
            auto const utf8_export_name_view = std::u8string_view(utf8_export_name);
            auto const cols = {u8"EXPORT"sv, utf8_path_name_view, utf8_export_name_view};
            csv_writer.write_row(cols);
        }

        std::wcout << m::to_wstring(buffer);
    }

    return EXIT_SUCCESS;
}
