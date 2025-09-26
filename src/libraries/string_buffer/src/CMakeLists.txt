cmake_minimum_required(VERSION 3.23)

target_sources(m_string_buffer PRIVATE
    string_buffer.cpp
)

target_include_directories(m_string_buffer PUBLIC
    ../include
)

target_link_libraries(m_string_buffer PUBLIC
    m_error_handling
    m_inplace_vector
    m_math
    m_pool
)
