cmake_minimum_required(VERSION 3.30)
project(pe)

add_library(pe STATIC
    pe_decoder.cpp
 "loader_context.cpp")

target_include_directories(pe PUBLIC
    ../include
)

target_link_libraries(pe PUBLIC
    byte_streams
    cast
    filesystem
    math
    memory
)

set_target_properties(pe PROPERTIES
    VERSION ${VERSION}
)
