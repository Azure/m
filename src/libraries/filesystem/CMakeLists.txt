cmake_minimum_required(VERSION 3.23)

add_library(m_filesystem STATIC)

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_filesystem
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
