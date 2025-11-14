cmake_minimum_required(VERSION 3.23)

find_package(Git)

if(Git_FOUND)
    message(STATUS "Git Found - getting tag description for source directory \"${CMAKE_SOURCE_DIR}\"")
    message(STATUS "Git executable: \"${GIT_EXECUTABLE}\"")

    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --long --match "v*"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE FROMGIT_RAW
        COMMAND_ECHO STDOUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    message(STATUS "describe reported: \"${FROMGIT_RAW}\"")

    if(FROMGIT_RAW STREQUAL "")
        set(FROMGIT_RAW "v0.0.0-nogithere")
    endif()

    string(REGEX MATCH "v[0-9]+\.[0-9]+\.[0-9]+" FULL_SOURCE_VERSION "${FROMGIT_RAW}")
    string(REGEX MATCH "[0-9]+\.[0-9]+\.[0-9]+" VERSION "${FROMGIT_RAW}")

    message(STATUS "Found Git - version ${VERSION}, full tag ${FULL_SOURCE_VERSION}")
else()
    set(VERSION 0.0.0)
    set(FULL_SOURCE_VERSION "v0.0.0-not-from-git")

    message(STATUS "Git Not Found - version ${VERSION}, full tag ${FULL_SOURCE_VERSION}")
endif()

project(m
    LANGUAGES CXX
    VERSION ${VERSION}
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)
include(FindGTest)

# Add custom CMake module
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Common)

set(M_CXX_STD cxx_std_20)

if(CMAKE_CXX_STANDARD LESS 20)
    message(FATAL_ERROR "The M project requires C++20 or later")
endif()

# de-duplicate linker inputs
# cmake_policy(SET CMP0156 NEW)

cmake_policy(SET CMP0082 NEW)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

include(CMakeDependentOption)

cmake_dependent_option(DISCOVER_TESTS "Discover tests within built tests" ON BUILD_TESTING OFF)

message(STATUS "Building tests: ${BUILD_TESTING}")
message(STATUS "Discovering tests: ${DISCOVER_TESTS}")

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

    string(REGEX REPLACE "/O[1-2]" "/Od" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/Ob[1-9]" "/Ob0" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    
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

set(M_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/m CACHE PATH "Root directory for M installed include files" FORCE)

message(STATUS "Project M: Using vcpkg toolchain file at ${CMAKE_TOOLCHAIN_FILE}")

set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

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
find_package(GTest CONFIG REQUIRED)

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

set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    CACHE PATH "Location of header files" )

set(SYSCONFIG_INSTALL_DIR ${CMAKE_INSTALL_SYSCONFDIR}/share/${PROJECT_NAME}
    CACHE PATH "Location of configuration files" )
#...

configure_package_config_file(
    ${PROJECT_NAME}-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
    INSTALL_DESTINATION ${SYSCONFIG_INSTALL_DIR}
    PATH_VARS INCLUDE_INSTALL_DIR SYSCONFIG_INSTALL_DIR
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
    ARCH_INDEPENDENT
)

configure_package_config_file(${PROJECT_NAME}-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
    INSTALL_DESTINATION ${SYSCONFIG_INSTALL_DIR}
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
    DESTINATION
        ${SYSCONFIG_INSTALL_DIR}
)
