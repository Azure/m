cmake_minimum_required(VERSION 3.23)

target_sources(m_print PRIVATE
    print.cpp
)

target_include_directories(m_print PUBLIC
    ../include
)

target_link_libraries(m_print PUBLIC
    m_error_handling
)
