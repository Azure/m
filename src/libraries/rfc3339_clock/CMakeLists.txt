cmake_minimum_required(VERSION 3.23)

add_library(m_rfc3339_clock STATIC)

target_compile_features(m_rfc3339_clock PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

list(APPEND m_installation_targets
    m_rfc3339_clock
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
