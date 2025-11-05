cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_error_handling)

add_executable(${TEST_EXE_NAME}
    test_error_handling.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_error_handling
    GTest::gtest_main
)

add_executable(test_internal_error_check
    test_internal_error_check.cpp
)

target_compile_features(test_internal_error_check PUBLIC ${M_CXX_STD})

target_link_libraries(test_internal_error_check
    m_error_handling
    GTest::gtest_main
)

enable_testing()

# do NOT add test_internal_error_check to the tests to discover! it fails (obviously!)
if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

