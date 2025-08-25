cmake_minimum_required(VERSION 3.23)

add_library(m_const_string STATIC)

target_compile_features(m_const_string PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_const_string
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
