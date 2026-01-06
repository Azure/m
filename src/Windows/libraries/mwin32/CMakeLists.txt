cmake_minimum_required(VERSION 3.23)

add_library(m_mwin32 SHARED mwin32.def)

target_compile_features(m_mwin32 PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

list(APPEND m_installation_targets
    m_mwin32
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
