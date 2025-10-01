cmake_minimum_required(VERSION 3.23)

target_sources(m_puddle PRIVATE
    puddle.cpp
)

target_include_directories(m_puddle PUBLIC
    ../include
)

target_link_libraries(m_puddle PUBLIC
    m_bitset
    m_error_handling
)
