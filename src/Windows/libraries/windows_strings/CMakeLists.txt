cmake_minimum_required(VERSION 3.23)

add_library(m_windows_strings STATIC)

target_compile_features(m_windows_strings PUBLIC cxx_std_23)

add_subdirectory(include)
add_subdirectory(src)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

list(APPEND m_installation_targets
    m_windows_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
