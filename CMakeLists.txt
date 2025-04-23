cmake_minimum_required(VERSION 3.30)

# de-duplicate linker inputs
cmake_policy(SET CMP0156 NEW)

cmake_policy(SET CMP0082 NEW)

project(m)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(M_VCPKG_MODE true)
else()
    set(M_VCPKG_MODE false)
endif()

# The tests build by default if the repo is accessed directly, not
# by default if included as a dependency via vcpkg. You may enable
# this by turning this option on.
if(M_VCPKG_MODE)
    option(M_BUILD_TESTS "Build the tests for M" OFF)
else()
    option(M_BUILD_TESTS "Build the tests for M" ON)
endif()

set(CMAKE_CXX_STANDARD 23)


set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Add custom modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD "23")

# Enable vcpkg integration if CMAKE_TOOLCHAIN_FILE is set
if(${M_VCPKG_MODE})
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

## Git (and its revision)
find_package(Git QUIET) # if we don't find git or FindGit.cmake is not on the system we ignore it.
## GetGitRevisionDescription module to retreive branch and revision information from Git
## Starting with Git 1.9 the module will be part of official cMake distribution, until then it has to be
## part of the application
## The Git module will trigger a reconfiguration for each pull that will bring a new revision on the local repository
set (VCS_REVISION "-1")
if(GIT_FOUND)
    include(GetGitRevisionDescription)
    get_git_head_revision(GIT_REFSPEC GIT_SHA1)
    message(STATUS "GIT branch ${GIT_REFSPEC}")
    message(STATUS "GIT revision ${GIT_SHA1}")
    set(VCS_REVISION ${GIT_SHA1})
endif()

# Set the version number of your project here (format is MAJOR.MINOR.PATCHLEVEL - e.g. 1.0.0)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "3")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# Dependencies
include(FetchContent)

if (${M_VCPKG_MODE})

    if (${M_BUILD_TESTS})
        include(FindGTest)

        find_package(GTest REQUIRED)
    endif()

else()

    if (${M_BUILD_TESTS})
        FetchContent_Declare(
          GTest
          GIT_REPOSITORY https://github.com/google/googletest.git
          GIT_TAG v1.16.0
        )

        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(GTest)
    endif()

    FetchContent_Declare(GSL
        GIT_REPOSITORY "https://github.com/microsoft/GSL"
        GIT_TAG "v4.2.0"
        GIT_SHALLOW ON
    )

    FetchContent_MakeAvailable(GSL)

endif()

# Global include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src/include
)

# Add project subdirectories
add_subdirectory(src)
