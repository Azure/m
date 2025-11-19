cmake_minimum_required(VERSION 3.23)

if(BUILD_TESTING)
add_subdirectory(test)
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
