cmake_minimum_required(VERSION 3.23)

add_library(m_block_buffer STATIC)

target_compile_features(m_block_buffer PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)

list(APPEND m_installation_targets
    m_block_buffer
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
