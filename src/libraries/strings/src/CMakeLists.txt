cmake_minimum_required(VERSION 3.23)

target_sources(m_strings PRIVATE
    cvt_char16_to_char16.cpp
    cvt_char16_to_char32.cpp
    cvt_char16_to_char8.cpp
    cvt_char32_to_char16.cpp
    cvt_char32_to_char8.cpp
    cvt_char8_to_char16.cpp
    cvt_char8_to_char32.cpp
    cvt_char8_to_char8.cpp
    punning.cpp
)

target_include_directories(m_strings PUBLIC
    ../include
)

target_include_directories(m_strings PRIVATE
    .
)

target_link_libraries(m_strings PUBLIC
    m_math
    m_utf
)

add_subdirectory(Platforms)
