cmake_minimum_required(VERSION 3.23)

if(WIN32)
add_subdirectory(Windows)
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
