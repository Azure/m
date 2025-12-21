cmake_minimum_required(VERSION 3.23)

target_sources(m_win32 PRIVATE
    event.cpp
    filetime_clock.cpp
    registry.cpp
    tp_io.cpp
    tp_timer.cpp
    tp_wait.cpp
    tp_work.cpp
    win32_threadpool.cpp
)

target_include_directories(m_win32 PUBLIC
    ../include
)

target_link_libraries(m_win32 PUBLIC
    m_cast
    m_errors
    m_error_handling
    m_sstring
    m_windows_chrono
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
