cmake_minimum_required(VERSION 3.23)

target_sources(m_linux_strings PRIVATE
    to_string.cpp
    to_wstring.cpp
)

target_include_directories(m_linux_strings PUBLIC
    ../include
)

target_link_libraries(m_linux_strings PUBLIC
    m_math
    m_strings
    m_utf
)

list(APPEND m_installation_targets
    m_linux_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
