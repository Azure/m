cmake_minimum_required(VERSION 3.23)

target_sources(m_thread_description PRIVATE
)

target_include_directories(m_thread_description PUBLIC
    ../include
)

target_link_libraries(m_thread_description PUBLIC
)

list(APPEND m_installation_targets
    m_thread_description
)

add_subdirectory(platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
