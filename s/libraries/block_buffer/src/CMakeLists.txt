cmake_minimum_required(VERSION 3.23)

target_sources(m_block_buffer PRIVATE
    block_buffer.cpp
)

target_include_directories(m_block_buffer PUBLIC
    ../include
)

target_link_libraries(m_block_buffer PUBLIC
    m_math
)
