cmake_minimum_required(VERSION 3.23)

add_library(m_cast INTERFACE)

target_compile_features(m_cast INTERFACE ${M_CXX_STD})

add_subdirectory(include)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

target_include_directories(m_cast INTERFACE
    include
)

target_link_libraries(m_cast INTERFACE
    m_math
)

list(APPEND m_installation_targets
    m_cast
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
