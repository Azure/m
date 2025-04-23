cmake_minimum_required(VERSION 3.22)

add_library(utf STATIC
    utf.cpp
    decode_utf8.cpp
    decode_utf16.cpp
    decode_utf32.cpp 
    encoding_size.cpp)

target_include_directories(utf PUBLIC
    ../include
)

target_link_libraries(utf PUBLIC
    math
)

set_target_properties(utf PROPERTIES
    VERSION ${VERSION}
)

