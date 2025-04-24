cmake_minimum_required(VERSION 3.23)

add_subdirectory(include)
add_subdirectory(libraries)
add_subdirectory(tools)

if(WIN32)
    add_subdirectory(Windows)
endif()

if(LINUX)
    add_subdirectory(Linux)
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
