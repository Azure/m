cmake_minimum_required(VERSION 3.23)

target_sources(m_windows_chrono PRIVATE
    windows_chrono_casts.cpp
)

target_include_directories(m_windows_chrono PUBLIC
    ../include/Platforms/Windows
)

target_link_libraries(m_windows_chrono PUBLIC
    m_cast
    m_chrono
    m_tracing
    m_windows_wrappers
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
