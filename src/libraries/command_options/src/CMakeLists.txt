cmake_minimum_required(VERSION 3.23)

target_sources(m_command_options PRIVATE
    command_options.cpp
)

target_compile_definitions(m_command_options PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_command_options PUBLIC
    m_cast
    m_filesystem
    m_math
    m_memory
    m_strings
)

target_include_directories(m_command_options PUBLIC
    ../include
)
