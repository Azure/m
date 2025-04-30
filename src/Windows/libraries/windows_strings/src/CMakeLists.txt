cmake_minimum_required(VERSION 3.23)

target_sources(m_windows_strings PRIVATE
    lib.cpp
)

target_include_directories(m_windows_strings PUBLIC
    ../include
)

target_link_libraries(m_windows_strings PUBLIC
    m_multi_byte
)

list(APPEND m_installation_targets
    m_windows_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
