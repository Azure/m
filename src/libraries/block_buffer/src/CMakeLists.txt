cmake_minimum_required(VERSION 3.22)

add_library(block_buffer STATIC
    block_buffer.cpp
)

target_include_directories(block_buffer PUBLIC
    ../include
)

target_link_libraries(block_buffer PUBLIC
    math
)
