cmake_minimum_required(VERSION 3.23)

target_sources(m_rco_ptr PRIVATE
    rco_ptr.cpp
)

target_include_directories(m_rco_ptr PUBLIC
    ../include
)

target_link_libraries(m_rco_ptr PUBLIC
    m_error_handling
)
