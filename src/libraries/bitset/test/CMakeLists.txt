cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_bitset)

add_executable(${TEST_EXE_NAME}
    test_bitset.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC cxx_std_23)

target_link_libraries(${TEST_EXE_NAME}
    m_bitset
    GTest::gtest_main
)

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

enable_testing()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
