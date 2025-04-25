cmake_minimum_required(VERSION 3.23)

# de-duplicate linker inputs
cmake_policy(SET CMP0156 NEW)

cmake_policy(SET CMP0082 NEW)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "9")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

project(m
    LANGUAGES CXX
    VERSION ${VERSION})

include(GNUInstallDirs)

set(M_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/m CACHE PATH "Root directory for M installed include files" FORCE)

if(DEFINED VCPKG_TARGET_TRIPLET)
    set(M_VCPKG_MODE true)
else()
    set(M_VCPKG_MODE false)
endif()

message(STATUS "Project M: VCPKG_TARGET_TRIPLET: '${VCPKG_TARGET_TRIPLET}' -> M_VCPKG_MODE: ${M_VCPKG_MODE}")
message(STATUS "Project M: Using vcpkg toolchain file at ${CMAKE_TOOLCHAIN_FILE}")

# The tests build by default if the repo is accessed directly, not
# by default if included as a dependency via vcpkg. You may enable
# this by turning this option on.
if(M_VCPKG_MODE)
    option(build_tests "Build the tests for M" OFF)
else()
    option(build_tests "Build the tests for M" ON)
endif()

#
# The logic for the defaulting of options is somehow weird
# perhaps use of CMake_condition_option() would fix it but
# it doesn't seem so.
#
# In practice, for vcpkg mode, we will assume that the
# options are correctly configured since they are always
# configured via the vcpkg.json manifest.
#
# For non-vcpkg mode, at least building using visual
# studio, the defaults are not honored and the developer
# has to go in to the CMakePresets.json and set the
# cache variables for the options in order for them to
# be recognized.
#
# So for non-vcpkg mode, we will just always build tests,
# with no way to avoid it for now. Sorry.
#

if(M_VCPKG_MODE)
    set(M_BUILD_TESTS ${build_tests} CACHE BOOL "Are the tests being built" FORCE)
else()
    set(M_BUILD_TESTS ON CACHE BOOL "Are the tests being built" FORCE)
endif()

set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Add custom CMake module
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD "23")

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

# Dependencies
include(FetchContent)

if (M_VCPKG_MODE)
    if (M_BUILD_TESTS)
        # include(FindGTest)
        find_package(GTest CONFIG REQUIRED)
    endif()
else()
    if (M_BUILD_TESTS)
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

list(APPEND m_installation_targets
    pe2csv
    pe2l
)

install(
    TARGETS ${m_installation_targets}
    FILE_SET HEADERS
#        DESTINATION ${M_INSTALL_INCLUDEDIR}
)

set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR}/m
    CACHE PATH "Location of header files" )

set(SYSCONFIG_INSTALL_DIR ${CMAKE_INSTALL_SYSCONFDIR}/share/m
    CACHE PATH "Location of configuration files" )
#...

include(CMakePackageConfigHelpers)

configure_package_config_file(
    m-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/m-config.cmake
    INSTALL_DESTINATION share/m
    PATH_VARS INCLUDE_INSTALL_DIR SYSCONFIG_INSTALL_DIR
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/m-configVersion.cmake
  VERSION ${VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/m-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/m-configVersion.cmake
    DESTINATION
        share/m )

