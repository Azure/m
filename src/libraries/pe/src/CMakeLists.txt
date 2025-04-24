cmake_minimum_required(VERSION 3.23)

target_sources(m_pe PRIVATE
    pe_decoder.cpp
    loader_context.cpp)

target_include_directories(m_pe PUBLIC
    ../include
)

target_link_libraries(m_pe PUBLIC
    m_byte_streams
    m_cast
    m_filesystem
    m_math
    m_memory
)
