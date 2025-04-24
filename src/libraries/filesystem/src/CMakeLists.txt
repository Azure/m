cmake_minimum_required(VERSION 3.23)

target_sources(m_filesystem PRIVATE
    file.cpp
    filesystem.cpp
    make_path.cpp)

target_compile_definitions(m_filesystem PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_filesystem PUBLIC
    m_block_buffer
    m_byte_streams
    m_cast
    m_math
    m_memory
    m_strings
)

target_include_directories(m_filesystem PUBLIC
    ../include
)
