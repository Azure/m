cmake_minimum_required(VERSION 3.30)

# de-duplicate linker inputs
cmake_policy(SET CMP0156 NEW)

cmake_policy(SET CMP0082 NEW)

project(m)

set(M_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD "23")

if(MSVC)
    add_compile_options(/bigobj)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

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
    set (VCS_REVISION ${GIT_SHA1})
endif()

# Set the version number of your project here (format is MAJOR.MINOR.PATCHLEVEL - e.g. 1.0.0)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "1")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY  https://github.com/google/googletest.git
  GIT_TAG v1.16.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

include_directories(
    ${gsl_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/include
)

add_subdirectory(src)

