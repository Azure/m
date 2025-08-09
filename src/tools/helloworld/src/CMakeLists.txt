cmake_minimum_required(VERSION 3.23)

add_executable(helloworld helloworld.cpp)

target_compile_features(helloworld PUBLIC cxx_std_23)

target_compile_definitions(helloworld PUBLIC _CRT_SECURE_NO_WARNINGS)

