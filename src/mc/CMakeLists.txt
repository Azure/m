cmake_minimum_required(VERSION 3.23)

add_library(m_mc STATIC)

target_compile_features(m_mc PUBLIC cxx_std_23)

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_mc
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
