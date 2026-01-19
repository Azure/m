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
#include <m/print/print.h>
#include <m/strings/convert.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

using namespace std::string_view_literals;

#include "dump.h"

struct encoded_file_buffer
{
    static inline constexpr std::size_t buffer_size = 1 << 17;

    encoded_file_buffer(std::filesystem::path const& path):
        m_buffer{},
        m_buffer_position{},
        m_path(path),
        m_seq_out(m::filesystem::make_sequential_output_file(m_path))
    {}

    void
    append_line_to_buffer(std::span<char8_t const> line)
    {
        if (line.size() > m_buffer.size())
        {
            // Huge line! Flush the buffer, write the line, start over from nothing.
            if (m_buffer_position != 0)
                flush_buffer();

            m_seq_out->write(std::as_bytes(line));
            return;
        }

        M_INTERNAL_ERROR_CHECK(m_buffer_position <= m_buffer.size());

        if ((m_buffer.size() - m_buffer_position) <= line.size())
        {
            flush_buffer();
        }

        std::ranges::copy(line, m_buffer.begin() + m_buffer_position);
        m_buffer_position += line.size();

        M_INTERNAL_ERROR_CHECK(m_buffer_position < m_buffer.size());
    }

    void
    flush_buffer()
    {
        if (m_buffer_position != 0)
        {
            m_seq_out->write(std::as_bytes(std::span(m_buffer.data(), m_buffer_position)));
            m_buffer_position = 0;
        }
    }

    std::array<char8_t, buffer_size>                       m_buffer;
    std::size_t                                            m_buffer_position;
    std::filesystem::path                                  m_path;
    std::shared_ptr<m::filesystem::sequential_output_file> m_seq_out;
};

void
m::pe2l::dump(m::not_null<m::command_options::parsed_command<char>*> pc)
{
    auto const path =
        std::filesystem::absolute(pc->get_parameter<std::filesystem::path>("filename"sv));
    auto const out = m::filesystem::combine(
        pc->get_option<std::filesystem::path>("out"sv), path, std::filesystem::path(u8".csv"));
    auto stream  = m::filesystem::open_seekable_input_file(path);
    auto decoder = std::make_unique<m::pe::decoder>(stream);

    encoded_file_buffer efb(out);
    m::csv::writer      cw([&](auto const spn) { efb.append_line_to_buffer(spn); });

    auto const parent_path = path.parent_path();
    auto const filename    = path.filename();

    std::array<m::filesystem::path_string_view, 4> exports = {
        M_FILESYSTEM_T("EXPORT"sv),
        m::filesystem::path_string_view(parent_path.c_str()),
        m::filesystem::path_string_view(filename.c_str()),
        m::filesystem::path_string_view()};

    for (auto const& e: decoder->m_image_export_directory.m_names)
    {
        auto const p = m::filesystem::to_path_string(e);
        exports[3]   = m::filesystem::path_string_view(p);
        cw.write_row(exports);
    }
}
