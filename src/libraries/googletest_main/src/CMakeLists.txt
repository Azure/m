cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

target_sources(m_googletest_main PRIVATE
    googletest_main.cpp
)

target_compile_definitions(m_googletest_main PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_googletest_main PUBLIC
    m_strings
    m_tracing
    GTest::gtest
)

target_include_directories(m_googletest_main PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
