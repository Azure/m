cmake_minimum_required(VERSION 3.23)

target_sources(m_arefc_ptr PRIVATE
    arefc_ptr.cpp
)

target_include_directories(m_arefc_ptr PUBLIC
    ../include
)

target_link_libraries(m_arefc_ptr PUBLIC
    m_error_handling
)
