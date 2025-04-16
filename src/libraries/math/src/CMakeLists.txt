cmake_minimum_required(VERSION 3.30)
project(math)

add_library(math STATIC
    math.cpp
)

target_include_directories(math PUBLIC
    ../include
)

target_link_libraries(math PUBLIC
#    math
    cast
)

set_target_properties(math PROPERTIES
    VERSION ${VERSION}
)

