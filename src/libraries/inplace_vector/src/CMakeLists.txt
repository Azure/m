cmake_minimum_required(VERSION 3.23)

target_sources(m_inplace_vector PRIVATE
    inplace_vector.cpp
)

target_include_directories(m_inplace_vector PUBLIC
    ../include
)

target_link_libraries(m_inplace_vector PUBLIC
    m_error_handling
)
