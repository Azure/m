cmake_minimum_required(VERSION 3.23)

add_executable(helloworld helloworld.cpp)

target_compile_definitions(helloworld PUBLIC _CRT_SECURE_NO_WARNINGS)

