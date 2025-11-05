cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_test_data)

add_executable(${TEST_EXE_NAME}
    test_test_data.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC cxx_std_23)

target_link_libraries(${TEST_EXE_NAME}
    m_print
    m_test_data
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

