cmake_minimum_required(VERSION 3.30)
project(cast)

add_library(cast STATIC
    cast.cpp
)

target_include_directories(cast PUBLIC
    ../include
)

set_target_properties(cast PROPERTIES
    VERSION ${VERSION}
)

