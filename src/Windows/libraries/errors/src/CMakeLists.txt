cmake_minimum_required(VERSION 3.30)

add_library(errors STATIC
    errors.cpp
    win32_error_code.cpp)

target_include_directories(errors PUBLIC
    ../include
)

target_link_libraries(errors PUBLIC
)

set_target_properties(errors PROPERTIES
    VERSION ${VERSION}
)

