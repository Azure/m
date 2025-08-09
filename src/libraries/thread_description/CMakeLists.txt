cmake_minimum_required(VERSION 3.23)

add_library(m_thread_description STATIC)

target_compile_features(m_thread_description PUBLIC cxx_std_23)

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_thread_description
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
