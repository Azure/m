cmake_minimum_required(VERSION 3.23)

target_sources(m_threadpool PRIVATE
    timer_base.cpp
    work_item.cpp
    work_queue_base.cpp
)

target_include_directories(m_threadpool PUBLIC
    ../include
)

target_link_libraries(m_threadpool PUBLIC
    m_chrono
    m_error_handling
    m_thread_description
)

list(APPEND m_installation_targets
    m_threadpool
)

add_subdirectory(Platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
