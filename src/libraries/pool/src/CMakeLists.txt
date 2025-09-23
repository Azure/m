cmake_minimum_required(VERSION 3.23)

target_sources(m_pool PRIVATE
    pool.cpp
)

target_include_directories(m_pool PUBLIC
    ../include
)

target_link_libraries(m_pool PUBLIC
    m_bitset
    m_error_handling
)
