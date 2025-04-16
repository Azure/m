cmake_minimum_required(VERSION 3.30)

project(pe2csv)

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

set_target_properties(pe2csv PROPERTIES
  VERSION ${VERSION}  # ${VERSION} was defined in the main CMakeLists.
)

