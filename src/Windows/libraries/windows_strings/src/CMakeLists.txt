cmake_minimum_required(VERSION 3.23)

target_sources(m_windows_strings PRIVATE
    char_to_char16.cpp
    char_to_char32.cpp
    char_to_char8.cpp
    char_to_wchar.cpp
    char16_to_char.cpp
    char16_to_wchar.cpp
    char32_to_char.cpp
    char32_to_wchar.cpp
    char8_to_char.cpp
    char8_to_wchar.cpp 
    lib.cpp
    view_to_u16string.cpp
    view_to_wstring.cpp
    wchar_to_char.cpp
    wchar_to_char16.cpp
    wchar_to_char32.cpp
    wchar_to_char8.cpp
)

target_include_directories(m_windows_strings PUBLIC
    ../include
)

target_link_libraries(m_windows_strings PUBLIC
    m_cp_acp
    m_multi_byte
    m_sstring
)

list(APPEND m_installation_targets
    m_windows_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
