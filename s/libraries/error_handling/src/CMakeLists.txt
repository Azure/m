cmake_minimum_required(VERSION 3.23)

target_sources(m_error_handling PRIVATE
    error_handling.cpp
)

target_include_directories(m_error_handling PUBLIC
    ../include
)

target_link_libraries(m_error_handling PUBLIC
    m_tracing
)
