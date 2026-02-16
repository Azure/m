cmake_minimum_required(VERSION 3.23)

add_library(m_math INTERFACE)

target_compile_features(m_math INTERFACE ${M_CXX_STD})

add_subdirectory(include)
if(BUILD_TESTING)
add_subdirectory(test)
endif()

target_include_directories(m_math INTERFACE
    include
)

target_link_libraries(m_math INTERFACE
    m_cast
)

list(APPEND m_installation_targets
    m_math
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
