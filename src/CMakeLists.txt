cmake_minimum_required(VERSION 3.22)

add_subdirectory(include)
add_subdirectory(libraries)
add_subdirectory(tools)

if(WIN32)
    add_subdirectory(Windows)
endif()

if(LINUX)
    add_subdirectory(Linux)
endif()
