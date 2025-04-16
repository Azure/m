cmake_minimum_required(VERSION 3.30)

add_library(strings STATIC
    convert.cpp
    punning.cpp
)

target_include_directories(strings PUBLIC
    ../include
)

target_link_libraries(strings PUBLIC
    math
    utf
)

set_target_properties(strings PROPERTIES
    VERSION ${VERSION}
)

