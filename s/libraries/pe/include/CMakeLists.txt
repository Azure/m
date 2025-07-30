cmake_minimum_required(VERSION 3.23)

target_sources(m_pe PUBLIC FILE_SET HEADERS FILES 
    m/pe/basic_types.h
    m/pe/file_offset_t.h
    m/pe/image_data_directory.h
    m/pe/image_dos_header.h
    m/pe/image_export_directory.h
    m/pe/image_file_header.h
    m/pe/image_import_by_name.h
    m/pe/image_import_descriptor.h
    m/pe/image_magic_t.h
    m/pe/image_nt_headers.h
    m/pe/image_optional_headers.h
    m/pe/image_section_header.h
    m/pe/loader_context.h
    m/pe/offset_t.h
    m/pe/pe.h
    m/pe/pe_decoder.h
    m/pe/position_t.h
    m/pe/rva_stream.h
    m/pe/rva_t.h
)
