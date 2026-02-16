cmake_minimum_required(VERSION 3.23)

add_library(m_atomic INTERFACE)

target_compile_features(m_atomic INTERFACE ${M_CXX_STD})

add_subdirectory(include)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

target_include_directories(m_atomic INTERFACE
    include
)

list(APPEND m_installation_targets
    m_atomic
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
