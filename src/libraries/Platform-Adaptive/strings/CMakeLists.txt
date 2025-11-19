cmake_minimum_required(VERSION 3.23)

add_library(m_platform_adaptive_strings STATIC)

target_compile_features(m_platform_adaptive_strings PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
# add_subdirectory(test)
if(BUILD_TESTING)
# add_subdirectory(test)
endif()

list(APPEND m_installation_targets
    m_platform_adaptive_strings
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
