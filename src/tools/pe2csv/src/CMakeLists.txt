cmake_minimum_required(VERSION 3.22)

add_executable(pe2csv
    main.cpp)

target_compile_definitions(pe2csv PUBLIC _CRT_SECURE_NO_WARNINGS)

target_link_libraries(pe2csv
    byte_streams
    command_options
    csv
    filesystem
    pe
)
