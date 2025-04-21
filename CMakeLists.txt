cmake_minimum_required(VERSION 3.30)

# Set CMake policies
cmake_policy(SET CMP0156 NEW)
cmake_policy(SET CMP0082 NEW)

# Project name
project(m)

# Enable C++23 standard
set(CMAKE_CXX_STANDARD 23)

# Set source root
set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Add custom modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Enable vcpkg integration if CMAKE_TOOLCHAIN_FILE is set
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    message(STATUS "Using vcpkg toolchain file at ${CMAKE_TOOLCHAIN_FILE}")
endif()

# MSVC specific settings
if(MSVC)
    add_compile_options(/bigobj)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

# Include common settings
include(Common)

# Git revision information
find_package(Git QUIET)
set(VCS_REVISION "-1")
if(GIT_FOUND)
    include(GetGitRevisionDescription)
    get_git_head_revision(GIT_REFSPEC GIT_SHA1)
    message(STATUS "GIT branch ${GIT_REFSPEC}")
    message(STATUS "GIT revision ${GIT_SHA1}")
    set(VCS_REVISION ${GIT_SHA1})
endif()

# Project version
set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "1")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# Enable testing
enable_testing()

# Dependencies
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Find any additional dependencies via vcpkg
find_package(gsl CONFIG REQUIRED) # Example: GSL (Guideline Support Library)

# Include directories
include_directories(
    ${gsl_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/include
)

# Add project subdirectories
add_subdirectory(src)
