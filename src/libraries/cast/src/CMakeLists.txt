cmake_minimum_required(VERSION 3.22)

add_library(cast STATIC
    cast.cpp
)

target_include_directories(cast PUBLIC
    ../include
)
