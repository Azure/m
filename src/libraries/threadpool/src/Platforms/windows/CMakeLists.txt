cmake_minimum_required(VERSION 3.23)

target_sources(m_threadpool PRIVATE
    periodic_timer.cpp
    threadpool.cpp
    timer.cpp
    work_queue.cpp
)

target_link_libraries(m_threadpool PUBLIC
    m_debugging
    m_errors
    m_error_handling
    m_formatters
    m_win32
    m_windows_chrono
)

target_include_directories(m_threadpool PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
