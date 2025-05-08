// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <variant>
#include <vector>

#include <m/byte_streams/byte_streams.h>
#include <m/math/math.h>
#include <m/memory/memory.h>

#include "pe_decoder.h"
#include "rva_stream.h"

#include "image_data_directory.h"
#include "image_dos_header.h"
#include "image_export_directory.h"
#include "image_file_header.h"
#include "image_import_descriptor.h"
#include "image_magic_t.h"
#include "image_nt_headers.h"
#include "image_optional_headers.h"
#include "image_section_header.h"

#include "image_data_directory.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        //
        // Tools to decode PE (Portable Executable) images.
        //
        // No particular discrimination between PE / PE32+ is made, the code will
        // attempt to be as compatible as possible with all variants available.
        //
        // Further, it avoids taking any dependencies on Windows header files as they
        // are unnecessary and fill the namespace with ugly type definitions.
        //
        // Instead we will treat the file metadata as a persistence format, where
        // in general we are interested in the data type and its offset from some
        // base. Yes, a "struct" does encode that but the specific byte offset from
        // the origin of the struct is implementation dependent. It seems
        // unnecessary to take such a dependency.
        //
        // We may regret this, but it's the path chosen.
        //

        class decoder
        {
            using position_t = io::position_t;
            using offset_t   = io::offset_t;

        public:
            decoder(std::shared_ptr<byte_streams::ra_in> const& ra_in);

            // private:
            // m_ra_in is the original un-remapped byte stream, also
            // available for reading.
            //

            using ra_in_t = byte_streams::ra_in;
            std::shared_ptr<ra_in_t> m_ra_in;

            using sp_ra_in_t  = std::shared_ptr<ra_in_t>;
            using rva_ra_in_t = m::pe::rva_ra_in<sp_ra_in_t>;

            //
            // This doesn't really *have* to be a separate allocation, but
            // it needs the section headers to have been computed which
            // has a host of other dependencies. For it to be constructed
            // in the constructor initializer list, the dependency chain is
            // unfortunately long.
            //
            // This *should* be addressed. There is no need for it and its
            // a point of fragility for no real purpose. On the other hand,
            // it's nice to make progres.
            //
            std::unique_ptr<rva_ra_in_t> m_rva_ra_in;

            image_file_header                m_image_file_header;
            image_magic_t                    m_image_magic;
            position_t                       m_image_optional_header_position;
            position_t                       m_data_directory_position;
            uint32_t                         m_number_of_rva_and_sizes;
            uint32_t                         m_file_alignment;
            uint32_t                         m_section_alignment;
            uint32_t                         m_size_of_image;
            uint32_t                         m_size_of_headers;
            uint16_t                         m_size_of_optional_header;
            uint16_t                         m_number_of_sections;
            image_data_directory             m_export_table;
            image_data_directory             m_import_table;
            image_data_directory             m_resource_table;
            image_data_directory             m_exception_table;
            image_data_directory_file_offset m_certificate_table;
            image_data_directory             m_base_relocation_table;
            image_data_directory             m_debug_table;
            image_data_directory             m_architecture_mbz;
            image_data_directory             m_global_ptr;
            image_data_directory             m_tls_table;
            image_data_directory             m_load_config_table;
            image_data_directory             m_bound_import;
            image_data_directory             m_iat;
            image_data_directory             m_delay_import_descriptor;
            image_data_directory             m_clr_runtime_header;
            image_data_directory             m_reserved_mbz;
            position_t                       m_section_headers_position;
            std::variant<image_optional_header32, image_optional_header64> m_image_optional_header;
            std::vector<image_section_header>                              m_section_headers;
            std::vector<image_import_descriptor> m_image_import_descriptors;
            image_export_directory               m_image_export_directory;

            template <typename, typename>
            friend struct std::formatter;
        };
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::decoder, wchar_t>
{
    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(m::pe::decoder const& decoder, FormatContext& ctx) const
    {
        auto iter = ctx.out();

        iter = std::format_to(iter,
                              L"{{ "
                              L"m_image_file_header: {}, "
                              L"m_image_magic: {}, "
                              L"m_data_directory_position: {}, "
                              L"m_number_of_rva_and_sizes: {}, "
                              L"m_file_alignment: {}, "
                              L"m_section_alignment: {}, "
                              L"m_size_of_image: {}, "
                              L"m_size_of_headers: {}, "
                              L"m_size_of_optional_header: {} "
                              L"}}",
                              decoder.m_image_file_header,
                              decoder.m_image_magic,
                              decoder.m_data_directory_position,
                              decoder.m_number_of_rva_and_sizes,
                              decoder.m_file_alignment,
                              decoder.m_section_alignment,
                              decoder.m_size_of_image,
                              decoder.m_size_of_headers,
                              decoder.m_size_of_optional_header);

        //
        // m_image_optional_header is a variant depending on whether this is a
        // pe32 or pe32+ image.
        //

        iter = std::format_to(iter,
                              L"{{ "
                              L"m_export_table: {}, "
                              L"m_import_table: {}, "
                              L"m_resource_table: {}, "
                              L"m_exception_table: {}, "
                              L"m_certificate_table: {}, "
                              L"m_base_relocation_table: {}, "
                              L"m_debug_table: {}, "
                              L"m_architecture_mbz: {}, "
                              L"m_global_ptr: {}, "
                              L"m_tls_table: {}, "
                              L"m_load_config_table: {}, "
                              L"m_bound_import: {}, "
                              L"m_iat: {}, "
                              L"m_delay_import_descriptor: {}, "
                              L"m_clr_runtime_header: {}, "
                              L"m_reserved_mbz: {} ",
                              decoder.m_export_table,
                              decoder.m_import_table,
                              decoder.m_resource_table,
                              decoder.m_exception_table,
                              decoder.m_certificate_table,
                              decoder.m_base_relocation_table,
                              decoder.m_debug_table,
                              decoder.m_architecture_mbz,
                              decoder.m_global_ptr,
                              decoder.m_tls_table,
                              decoder.m_load_config_table,
                              decoder.m_bound_import,
                              decoder.m_iat,
                              decoder.m_delay_import_descriptor,
                              decoder.m_clr_runtime_header,
                              decoder.m_reserved_mbz);

        iter = std::format_to(iter, L"m_section_headers: [ ");

        size_t counter{};

        for (auto&& e: decoder.m_section_headers)
        {
            if (counter != 0)
                iter = std::format_to(iter, L", ");

            iter = std::format_to(iter, L"[{}]: {}", counter, e);
            counter++;
        }

        iter = std::format_to(iter, L"], ");

        iter = std::format_to(iter, L"m_image_import_descriptors: [ ");

        counter = 0;

        for (auto&& e: decoder.m_image_import_descriptors)
        {
            if (counter != 0)
                iter = std::format_to(iter, L", ");

            iter = std::format_to(iter, L"[{}]: {}", counter, e);
            counter++;
        }

        iter = std::format_to(iter, L"], ");

        iter = std::format_to(
            iter, L"m_image_export_directory: {} ", decoder.m_image_export_directory);

        iter = std::format_to(iter, L"}}");

        return iter;
    }
};
