cmake_minimum_required(VERSION 3.30)
project(byte_streams)

# Define the library
add_library(byte_streams STATIC
    byte_streams.cpp
    memory_stream.cpp
)

# target_compile_definitions(filesystem PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(byte_streams PUBLIC
#    block_buffer
    cast
    io
    math
#    memory
)

target_include_directories(
    byte_streams
        PUBLIC
            ../include
)

set_target_properties(byte_streams PROPERTIES
    VERSION ${VERSION}
)

