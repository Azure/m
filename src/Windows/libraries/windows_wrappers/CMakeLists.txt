cmake_minimum_required(VERSION 3.23)

add_library(m_windows_wrappers STATIC)

target_compile_features(m_windows_wrappers PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_windows_wrappers
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
