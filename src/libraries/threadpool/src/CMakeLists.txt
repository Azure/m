cmake_minimum_required(VERSION 3.23)

target_sources(m_threadpool PRIVATE
)

target_include_directories(m_threadpool PUBLIC
    ../include
)

target_link_libraries(m_threadpool PUBLIC
    m_thread_description
)

list(APPEND m_installation_targets
    m_threadpool
)

add_subdirectory(platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
