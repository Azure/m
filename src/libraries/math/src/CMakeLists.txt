cmake_minimum_required(VERSION 3.22)

add_library(math STATIC
    math.cpp
)

target_include_directories(math PUBLIC
    ../include
)

target_link_libraries(math PUBLIC
    cast
)
