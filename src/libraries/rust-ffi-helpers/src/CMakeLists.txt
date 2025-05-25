cmake_minimum_required(VERSION 3.23)

target_sources(m_rust_ffi_helpers PRIVATE
    arc_ptr.cpp
)

target_include_directories(m_rust_ffi_helpers PUBLIC
    ../include
)

target_link_libraries(m_rust_ffi_helpers PUBLIC
    m_strings
    m_utf
)
