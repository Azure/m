cmake_minimum_required(VERSION 3.23)

target_sources(m_rust_ffi_helpers PUBLIC FILE_SET HEADERS FILES 
    m/rust-ffi-helpers/arc_ptr.h 
 "m/rust-ffi-helpers/string_in.h")
