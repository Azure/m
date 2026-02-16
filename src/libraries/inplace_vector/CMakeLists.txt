cmake_minimum_required(VERSION 3.23)

add_library(m_inplace_vector INTERFACE)

target_compile_features(m_inplace_vector INTERFACE ${M_CXX_STD})

add_subdirectory(include)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

target_include_directories(m_inplace_vector INTERFACE
    include
)

target_link_libraries(m_inplace_vector INTERFACE
    m_error_handling
)

list(APPEND m_installation_targets
    m_inplace_vector
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
