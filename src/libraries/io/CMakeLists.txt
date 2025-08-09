cmake_minimum_required(VERSION 3.23)

add_library(m_io STATIC)

target_compile_features(m_io PUBLIC cxx_std_23)

add_subdirectory(include)
add_subdirectory(src)

list(APPEND m_installation_targets
    m_io
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
