cmake_minimum_required(VERSION 3.23)

target_sources(m_windows_strings PUBLIC FILE_SET HEADERS FILES
    m/windows_strings/convert.h
    m/windows_strings/cvt_char_to_char8.h
    m/windows_strings/cvt_char_to_char16.h
    m/windows_strings/cvt_char_to_char32.h
    m/windows_strings/cvt_char_to_wchar.h
    m/windows_strings/cvt_char16_to_char.h
    m/windows_strings/cvt_char16_to_wchar.h
    m/windows_strings/cvt_char32_to_char.h
    m/windows_strings/cvt_char32_to_wchar.h
    m/windows_strings/cvt_char8_to_char.h
    m/windows_strings/cvt_char8_to_wchar.h
    m/windows_strings/cvt_wchar_to_char.h
    m/windows_strings/cvt_wchar_to_char8.h
    m/windows_strings/cvt_wchar_to_char16.h
    m/windows_strings/cvt_wchar_to_char32.h
    m/windows_strings/cvt_from_char8.h
    m/windows_strings/cvt_from_char16.h
    m/windows_strings/cvt_from_char32.h
    m/windows_strings/cvt_from_wchar.h
    m/windows_strings/views_char16_wchar.h
)
