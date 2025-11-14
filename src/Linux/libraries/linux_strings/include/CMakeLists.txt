cmake_minimum_required(VERSION 3.23)

target_sources(m_linux_strings PUBLIC FILE_SET HEADERS FILES
    m/linux_strings/compare.h
    m/linux_strings/convert.h
    m/linux_strings/cvt_char_to_char8.h
    m/linux_strings/cvt_char_to_char16.h
    m/linux_strings/cvt_char_to_char32.h
    m/linux_strings/cvt_char_to_wchar.h
    m/linux_strings/cvt_char8_to_char.h
    m/linux_strings/cvt_char8_to_wchar.h
    m/linux_strings/cvt_char16_to_char.h
    m/linux_strings/cvt_char16_to_wchar.h
    m/linux_strings/cvt_char32_to_char.h
    m/linux_strings/cvt_char32_to_wchar.h
    m/linux_strings/cvt_views.h
    m/linux_strings/cvt_wchar_to_char.h
    m/linux_strings/cvt_wchar_to_char8.h
    m/linux_strings/cvt_wchar_to_char16.h
    m/linux_strings/cvt_wchar_to_char32.h
)
