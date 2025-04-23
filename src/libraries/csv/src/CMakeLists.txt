cmake_minimum_required(VERSION 3.22)

add_library(csv STATIC
    csv.cpp
    breaker.cpp)

target_include_directories(csv PUBLIC
    ../include
)

target_link_libraries(csv PUBLIC
    io
    math
)
