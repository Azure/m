cmake_minimum_required(VERSION 3.23)

add_library(m_pool INTERFACE)

target_compile_features(m_pool INTERFACE ${M_CXX_STD})

add_subdirectory(include)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

target_include_directories(m_pool INTERFACE
    include
)

target_link_libraries(m_pool INTERFACE
    m_bitset
    m_error_handling
    m_puddle
)

list(APPEND m_installation_targets
    m_pool
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
