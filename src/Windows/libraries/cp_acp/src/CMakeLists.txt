cmake_minimum_required(VERSION 3.23)

target_sources(m_cp_acp PRIVATE
    acp_to.cpp
    acp_to_char.cpp
    acp_to_char16.cpp
    acp_to_char32.cpp
    acp_to_char8.cpp
    acp_to_utf16.cpp
    acp_to_wchar.cpp
    to_acp_string.cpp
    utf16_to_acp.cpp
)

target_include_directories(m_cp_acp PUBLIC
    ../include
)

target_link_libraries(m_cp_acp PUBLIC
    m_errors
    m_math
    m_multi_byte
    m_strings
    m_utf
)

list(APPEND m_installation_targets
    m_cp_acp
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
