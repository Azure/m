cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

target_sources(m_googletest PRIVATE
    temporary_directory_impl.cpp
    temporary_directory_manager.cpp
)

target_compile_definitions(m_googletest PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_googletest PUBLIC
    m_strings
    GTest::gtest_main
)

if(WIN32)
    target_link_libraries(m_googletest PUBLIC
    )
else()
    target_link_libraries(m_googletest PUBLIC
    )
endif()

target_include_directories(m_googletest PUBLIC
    ../include
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
