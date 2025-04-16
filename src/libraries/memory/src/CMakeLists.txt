cmake_minimum_required(VERSION 3.30)
project(memory)

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

set_target_properties(memory PROPERTIES
    VERSION ${VERSION}
)

