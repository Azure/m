cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_error_handling
    test_error_handling.cpp
)

target_compile_features(test_error_handling PUBLIC ${M_CXX_STD})

target_link_libraries(test_error_handling
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
gtest_discover_tests(test_error_handling)

