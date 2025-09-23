cmake_minimum_required(VERSION 3.23)

target_sources(m_bitset PRIVATE
    bitset.cpp
)

target_include_directories(m_bitset PUBLIC
    ../include
)

target_link_libraries(m_bitset PUBLIC
    m_error_handling
)
