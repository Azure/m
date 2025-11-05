cmake_minimum_required(VERSION 3.23)

set(M_CXX_STD cxx_std_20)

if(CMAKE_CXX_STANDARD LESS 20)
    message(FATAL_ERROR "The M project requires C++20 or later")
endif()

# de-duplicate linker inputs
# cmake_policy(SET CMP0156 NEW)

cmake_policy(SET CMP0082 NEW)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "10")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

project(m
    LANGUAGES CXX
    VERSION ${VERSION}
)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

include(CMakeDependentOption)

cmake_dependent_option(DISCOVER_TESTS "Discover tests within built tests" ON BUILD_TESTING OFF)


option(disable_native_wchar_t "Disable the use of wchar_t as a native distinct type" OFF)

if (MSVC)
    add_compile_options(/W4 /WX)
#     add_compile_options(/Wall)
    add_compile_options(/wd4018)        # '_': signed/unsigned mismatch [builtin relop]
    add_compile_options(/wd4324)        # '_': structure was padded due to alignment specifier
    add_compile_options(/wd4459)        # declaration of 'foo' hides global declaration
    add_compile_options(/wd4514)        # 'x': unreferenced inline function has been removed
    add_compile_options(/wd4625)        # 'x': copy constructor was implicitly defined as deleted
    add_compile_options(/wd4626)        # 'x': assignment operator was implicitly defined as deleted
    add_compile_options(/wd4710)        # 'x': function not inlined
    add_compile_options(/wd4820)        # 'n' bytes padding added after data member 'foo'
    add_compile_options(/wd5026)        # 'x': move constrcutor was implicitly defined as deleted
    add_compile_options(/wd5027)        # 'x': move assignment operator was implicitly defined as deleted
    add_compile_options(/wd5039)        # 'x': pointer or reference to potentially throwing function passed to 'extern "C"'
    add_compile_options(/we26447)       # The function is declared noexcept but calls function function_name that may throw exceptions (f.6).

    # string(REGEX REPLACE "/O[1-2]" "/Od" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # string(REGEX REPLACE "/Ob[1-9]" "/Ob0" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    
    # work around what appears to be a LTCG bug in MSVC
    string(REGEX REPLACE "/GL" "/GL-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    if(disable_native_wchar_t)
        add_compile_options("/Zc:wchar_t-")
    endif()

    add_compile_options(/bigobj)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

add_link_options(
    /LARGEADDRESSAWARE
    /DEBUG
    /INCREMENTAL:NO
    $<$<NOT:$<CONFIG:DEBUG>>:/OPT:REF> # Remove unreferenced functions and data.
    $<$<NOT:$<CONFIG:DEBUG>>:/OPT:ICF> # Identical COMDAT folding.
    $<$<CONFIG:DEBUG>:/OPT:NOREF> # No unreferenced data elimination.
    $<$<CONFIG:DEBUG>:/OPT:NOICF> # No Identical COMDAT folding.
)


else()
    add_compile_options(-Wall -Wextra -pedantic -Werror -Wno-unknown-pragmas)
    add_compile_options(-Wno-sign-compare)
    add_compile_options(-Wno-unneeded-internal-declaration)
#    add_compile_options(-wno-unused-local-typedef)          # error : unused type alias 'x'
endif()

include(CheckLinkerFlag)

check_linker_flag(CXX "LINKER:-natvis" HAS_LINKER_NATVIS)

include(GNUInstallDirs)

set(M_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/m CACHE PATH "Root directory for M installed include files" FORCE)

if(DEFINED VCPKG_TARGET_TRIPLET)
    set(M_VCPKG_MODE true)
else()
    set(M_VCPKG_MODE false)
endif()

message(STATUS "Project M: VCPKG_TARGET_TRIPLET: '${VCPKG_TARGET_TRIPLET}' -> M_VCPKG_MODE: ${M_VCPKG_MODE}")
message(STATUS "Project M: Using vcpkg toolchain file at ${CMAKE_TOOLCHAIN_FILE}")

set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Add custom CMake module
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)


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
    include(FindGTest)
    find_package(GTest CONFIG REQUIRED)
else()
    FetchContent_Declare(
        GTest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(GTest)

    FetchContent_Declare(GSL
        GIT_REPOSITORY "https://github.com/microsoft/GSL"
        GIT_TAG "v4.2.0"
        GIT_SHALLOW ON
    )

    FetchContent_MakeAvailable(GSL)
endif()

enable_testing()

# Global include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src/include
)

# Add project subdirectories
add_subdirectory(src)

# add_subdirectory(docs)

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
    INSTALL_DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/share/m
    PATH_VARS INCLUDE_INSTALL_DIR SYSCONFIG_INSTALL_DIR
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/m-config-version.cmake
  VERSION ${VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/m-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/m-config-version.cmake
    DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/share/m )

