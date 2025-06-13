cmake_minimum_required(VERSION 3.23)

target_sources(m_memory PRIVATE
    memory.cpp
)

target_include_directories(m_memory PUBLIC
    ../include
)

target_link_libraries(m_memory PUBLIC
    m_block_buffer
    m_math
)


set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
