cmake_minimum_required(VERSION 3.23)

target_sources(m_mwin32 PRIVATE
    handle_table.cpp
    mwinreg.cpp
)

target_include_directories(m_mwin32 PUBLIC
    ../include
)

target_link_libraries(m_mwin32 PUBLIC
    m_cast
    m_errors
    m_error_handling
    m_pil
    m_sstring
    m_windows_chrono
    m_windows_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
