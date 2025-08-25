cmake_minimum_required(VERSION 3.23)

target_sources(m_sstring PRIVATE
    sstring.cpp
)

target_include_directories(m_sstring PUBLIC
    ../include
)

target_link_libraries(m_sstring PUBLIC
    m_const_string
    m_error_handling
)
