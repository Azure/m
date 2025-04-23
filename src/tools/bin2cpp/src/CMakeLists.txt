cmake_minimum_required(VERSION 3.22)

set(APP_SOURCES
  bin2cpp.cpp
)

# Define two variables in order not to repeat ourselves.
set(APP_NAME bin2cpp)

add_executable(${APP_NAME})

target_sources(${APP_NAME} PRIVATE ${APP_SOURCES})

target_compile_definitions(${APP_NAME} PUBLIC _CRT_SECURE_NO_WARNINGS)
