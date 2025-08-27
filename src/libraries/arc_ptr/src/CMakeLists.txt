cmake_minimum_required(VERSION 3.23)

target_sources(m_arc_ptr PRIVATE
    arc_ptr.cpp
)

target_include_directories(m_arc_ptr PUBLIC
    ../include
)

target_link_libraries(m_arc_ptr PUBLIC
    m_error_handling
)
