cmake_minimum_required(VERSION 3.30)
project(bin2cpp)

set(APP_SOURCES
  bin2cpp.cpp
)

# Define two variables in order not to repeat ourselves.
set(APP_NAME bin2cpp)

add_executable(${APP_NAME})

target_sources(${APP_NAME} PRIVATE ${APP_SOURCES})

target_compile_definitions(${APP_NAME} PUBLIC _CRT_SECURE_NO_WARNINGS)

# Set the build version. It will be used in the name of the lib, with corresponding
# symlinks created. SOVERSION could also be specified for api version. 
set_target_properties(${APP_NAME} PROPERTIES
  VERSION ${VERSION}  # ${VERSION} was defined in the main CMakeLists.
)

