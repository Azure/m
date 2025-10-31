cmake_minimum_required(VERSION 3.23)

target_sources(m_strings PRIVATE
    convert_to_string.cpp
    convert_to_u8string.cpp
    convert_to_u16string.cpp
    convert_to_u32string.cpp
    convert_to_wstring.cpp
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
