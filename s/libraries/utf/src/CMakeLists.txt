cmake_minimum_required(VERSION 3.23)

target_sources(m_utf PRIVATE
    utf.cpp
    decode_utf8.cpp
    decode_utf16.cpp
    decode_utf32.cpp 
    encoding_size.cpp)

target_include_directories(m_utf PUBLIC
    ../include
)

target_link_libraries(m_utf PUBLIC
    m_math
)
