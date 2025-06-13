cmake_minimum_required(VERSION 3.23)

add_library(m_debugging STATIC)

add_subdirectory(include)
add_subdirectory(src)

list(APPEND m_installation_targets
    m_debugging
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
