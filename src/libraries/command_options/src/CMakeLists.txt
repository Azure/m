cmake_minimum_required(VERSION 3.22)

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

target_include_directories(command_options PUBLIC
    ../include
)
