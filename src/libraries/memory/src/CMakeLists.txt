cmake_minimum_required(VERSION 3.22)

add_library(memory STATIC
    memory.cpp
)

target_include_directories(memory PUBLIC
    ../include
)

target_link_libraries(memory PUBLIC
    block_buffer
    math
)
