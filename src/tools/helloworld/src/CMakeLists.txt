cmake_minimum_required(VERSION 3.30)
project(helloworld)

add_executable(helloworld helloworld.cpp)

target_compile_definitions(helloworld PUBLIC _CRT_SECURE_NO_WARNINGS)

set_target_properties(helloworld PROPERTIES
    VERSION ${VERSION}
)

