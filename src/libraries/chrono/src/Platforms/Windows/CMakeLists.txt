cmake_minimum_required(VERSION 3.23)

target_sources(m_chrono PRIVATE
    windows_chrono_casts.cpp
)

target_include_directories(m_chrono PUBLIC
    ../../../include/Platforms/Windows
)

target_link_libraries(m_chrono PUBLIC
    m_cast
    m_tracing
    m_windows_wrappers
)
