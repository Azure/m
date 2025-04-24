cmake_minimum_required(VERSION 3.23)

target_sources(m_strings PRIVATE
    convert.cpp
    punning.cpp
)

target_include_directories(m_strings PUBLIC
    ../include
)

target_link_libraries(m_strings PUBLIC
    m_math
    m_utf
)
