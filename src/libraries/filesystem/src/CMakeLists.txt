cmake_minimum_required(VERSION 3.30)
project(filesystem)

# Define the library
add_library(filesystem STATIC
    file.cpp
    filesystem.cpp
 make_path.cpp)

target_compile_definitions(filesystem PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(filesystem PUBLIC
    block_buffer
    byte_streams
    cast
    math
    memory
    strings
)

target_include_directories(
    filesystem
        PUBLIC
            ../include
)

set_target_properties(filesystem PROPERTIES
    VERSION ${VERSION}
)

