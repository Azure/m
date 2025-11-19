cmake_minimum_required(VERSION 3.23)

add_library(m_random STATIC)

target_compile_features(m_random PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

list(APPEND m_installation_targets
    m_random
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
