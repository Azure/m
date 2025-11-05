cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_utility)

add_executable(${TEST_EXE_NAME}
    test_pointers.cpp
)

target_compile_features(test_utility PUBLIC ${M_CXX_STD})

target_link_libraries(test_utility
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

list(APPEND m_installation_targets)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

