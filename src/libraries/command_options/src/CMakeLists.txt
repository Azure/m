cmake_minimum_required(VERSION 3.30)
project(command_options)

# Define the library
add_library(command_options STATIC
    command_options.cpp
)

target_compile_definitions(command_options PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(command_options PUBLIC
    cast
    filesystem
    math
    memory
    strings
)

target_include_directories(
    command_options
        PUBLIC
            ../include
)

set_target_properties(command_options PROPERTIES
    VERSION ${VERSION}
)

