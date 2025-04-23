cmake_minimum_required(VERSION 3.22)

add_library(io STATIC
    io.cpp
)

target_link_libraries(io PUBLIC
    cast
    math
)

target_include_directories(io PUBLIC
    ../include
)
