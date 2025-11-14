cmake_minimum_required(VERSION 3.23)

target_sources(m_linux_strings PRIVATE
    convert_string_view_to_string.cpp
    cvt_char_to_char8.cpp
    cvt_char_to_char16.cpp
    cvt_char_to_char32.cpp
    cvt_char_to_wchar.cpp
    cvt_char16_to_char.cpp
    cvt_char16_to_wchar.cpp
    cvt_char32_to_char.cpp
    cvt_char32_to_wchar.cpp
    cvt_char8_to_char.cpp
    cvt_char8_to_wchar.cpp
    cvt_wchar_to_char.cpp
    cvt_wchar_to_char8.cpp
    cvt_wchar_to_char16.cpp
    cvt_wchar_to_char32.cpp
    to_string.cpp
    to_wstring.cpp
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
