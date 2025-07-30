// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <stdexcept>

#include <m/memory/memory.h>
#include <m/pe/pe_decoder.h>

m::pe::decoder::decoder(std::shared_ptr<byte_streams::ra_in> const& ra_in): m_ra_in(ra_in)
{
    io::position_t origin = position_t{0};

    auto signature = m::load_from<uint16_t>(m_ra_in, origin + image_dos_header::k_offset_magic);
    auto lfanew    = m::load_from<int32_t>(m_ra_in, origin + image_dos_header::k_offset_lfanew);

    if (signature != image_dos_header::k_magic_mark_zibokowski)
        throw std::runtime_error("not a pe");

    //
    // The struct definitions that are in the platform SDK make it appear as if things are
    // laid out in a logical fashion but really you have to read ahead in order to know how
    // to interpret the current position.
    //
    // This isn't a big deal if you have the file mapped and you're changing which struct
    // pointer you are viewing the file through but where we're dealing with a possibly
    // streamed view of the file, reading bytes at a time, we have to approach is in a more
    // precise fashion.
    //
    // The first step is to recognize that the lfanew gives the offset into the file for the
    // IMAGE_NT_HEADER{32|64}. The first four bytes are the PE signature; read it and verify.
    //

    auto image_nt_headers_position = position_t(lfanew);

    // static_assert(image_nt_headers32::k_offset_signature ==
    // image_nt_headers64::k_offset_signature);

    constexpr uint32_t p_e_const = static_cast<uint32_t>('P') | static_cast<uint32_t>('E') << 8;

    auto pe_signature = m::load_from<uint32_t>(
        m_ra_in, image_nt_headers_position + image_nt_headers32::k_offset_signature);

    if (pe_signature != p_e_const)
        throw std::runtime_error("not a pe");

    auto const image_file_header_position =
        image_nt_headers_position + image_nt_headers32::k_offset_file_header;

    static_assert(image_nt_headers32::k_offset_optional_header ==
                  image_nt_headers64::k_offset_optional_header);
    m_image_optional_header_position =
        image_nt_headers_position + image_nt_headers32::k_offset_optional_header;

    static_assert(image_optional_header32::k_offset_magic ==
                  image_optional_header64::k_offset_magic);
    auto image_magic = m::load_from<m::pe::image_magic_t>(
        m_ra_in, m_image_optional_header_position + image_optional_header32::k_offset_magic);

    if ((image_magic != m::pe::image_magic_t::pe32) &&
        (image_magic != m::pe::image_magic_t::pe32plus))
        throw std::runtime_error("not a pe");

    m_image_magic = image_magic;

    m_image_file_header       = image_file_header::load_from(m_ra_in, image_file_header_position);
    m_size_of_optional_header = m_image_file_header.m_size_of_optional_header;
    m_number_of_sections      = m_image_file_header.m_number_of_sections;

    switch (image_magic)
    {
        default: throw std::runtime_error("unhandled case");

        case m::pe::image_magic_t::pe32:
        {
            m_image_optional_header = image_optional_header32::load_from(
                m_ra_in, m_image_optional_header_position, m_size_of_optional_header);
            auto ioh                  = std::get<image_optional_header32>(m_image_optional_header);
            m_file_alignment          = ioh.m_file_alignment;
            m_section_alignment       = ioh.m_section_alignment;
            m_size_of_image           = ioh.m_size_of_image;
            m_size_of_headers         = ioh.m_size_of_headers;
            m_number_of_rva_and_sizes = ioh.m_number_of_rva_and_sizes;
            m_data_directory_position =
                m_image_optional_header_position + image_optional_header32::k_offset_data_directory;

            break;
        }

        case m::pe::image_magic_t::pe32plus:
        {
            m_image_optional_header = image_optional_header64::load_from(
                m_ra_in, m_image_optional_header_position, m_size_of_optional_header);
            auto ioh                  = std::get<image_optional_header64>(m_image_optional_header);
            m_file_alignment          = ioh.m_file_alignment;
            m_section_alignment       = ioh.m_section_alignment;
            m_size_of_image           = ioh.m_size_of_image;
            m_size_of_headers         = ioh.m_size_of_headers;
            m_number_of_rva_and_sizes = ioh.m_number_of_rva_and_sizes;
            m_data_directory_position =
                m_image_optional_header_position + image_optional_header64::k_offset_data_directory;

            break;
        }
    }

    if (m_number_of_rva_and_sizes > k_directory_entry_export)
        m_export_table = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_export_table, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_import)
        m_import_table = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_import_table, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_resource)
        m_resource_table = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_resource_table, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_exception)
        m_exception_table = image_data_directory::load_from(m_ra_in,
                                                            m_data_directory_position,
                                                            k_offset_exception_table,
                                                            m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_certificate)
        m_certificate_table =
            image_data_directory_file_offset::load_from(m_ra_in,
                                                        m_data_directory_position,
                                                        k_offset_certificate_table,
                                                        m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_base_relocation)
        m_base_relocation_table = image_data_directory::load_from(m_ra_in,
                                                                  m_data_directory_position,
                                                                  k_offset_base_relocation_table,
                                                                  m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_debug)
        m_debug_table = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_debug_table, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_architecture_mbz)
        m_architecture_mbz = image_data_directory::load_from(m_ra_in,
                                                             m_data_directory_position,
                                                             k_offset_architecture_mbz,
                                                             m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_global_ptr)
        m_global_ptr = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_global_ptr, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_tls)
        m_tls_table = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_tls_table, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_load_config)
        m_load_config_table = image_data_directory::load_from(m_ra_in,
                                                              m_data_directory_position,
                                                              k_offset_load_config_table,
                                                              m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_bound_import)
        m_bound_import = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_bound_import, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_iat)
        m_iat = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_iat, m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_delay_import)
        m_delay_import_descriptor =
            image_data_directory::load_from(m_ra_in,
                                            m_data_directory_position,
                                            k_offset_delay_import_descriptor,
                                            m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_clr_runtime)
        m_clr_runtime_header = image_data_directory::load_from(m_ra_in,
                                                               m_data_directory_position,
                                                               k_offset_clr_runtime_header,
                                                               m_size_of_optional_header);

    if (m_number_of_rva_and_sizes > k_directory_entry_reserved_mbz)
        m_reserved_mbz = image_data_directory::load_from(
            m_ra_in, m_data_directory_position, k_offset_reserved_mbz, m_size_of_optional_header);

    m_section_headers_position = m_image_optional_header_position + m_size_of_optional_header;
    m_section_headers.reserve(m_number_of_sections);

    offset_t current_section_header_offset{};
    uint16_t section_index{};

    while (section_index < m_number_of_sections)
    {
        m_section_headers.emplace_back(image_section_header::load_from(
            m_ra_in, m_section_headers_position, current_section_header_offset));
        current_section_header_offset =
            current_section_header_offset + image_section_header::k_size;
        section_index++;
    }

    //
    // Phew! Now we can create the rva-mapped random access stream
    //

    m_rva_ra_in = std::make_unique<rva_ra_in_t>(
        m_ra_in, m_number_of_sections, [&](std::size_t i) -> rva_ra_in_t::section_header_data {
            return rva_ra_in_t::section_header_data{
                .m_virtual_address     = m_section_headers[i].m_virtual_address,
                .m_size_of_raw_data    = m_section_headers[i].m_size_of_raw_data,
                .m_pointer_to_raw_data = m_section_headers[i].m_pointer_to_raw_data,
            };
        });

    // Set up a base loading context for the import descriptor and what comes perhaps after?
    m::pe::load_from_rva_context lfrc(m_rva_ra_in.get(), m_import_table.m_virtual_address);

    m_image_import_descriptors.reserve(m_import_table.m_size / image_import_descriptor::k_size);

    std::size_t index{};

    while ((image_import_descriptor::k_size * (index + 1)) <= m_import_table.m_size)
    {
        auto import = m_image_import_descriptors.emplace_back(image_import_descriptor::load_from(
            m_rva_ra_in.get(), m_image_magic, m_import_table, index));

        if (import.is_zero())
            break;

        index++;
    }

    if (m_export_table.m_size != 0)
    {
        m_image_export_directory =
            image_export_directory::load_from(m_rva_ra_in.get(), m_image_magic, m_export_table);
    }
}
