cmake_minimum_required(VERSION 3.23)

target_sources(m_const_string PRIVATE
    const_string.cpp
)

target_include_directories(m_const_string PUBLIC
    ../include
)

target_link_libraries(m_const_string PUBLIC
    m_error_handling
    m_rco_ptr
)
