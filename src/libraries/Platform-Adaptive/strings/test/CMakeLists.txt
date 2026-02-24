cmake_minimum_required(VERSION 3.23)

project(m)

include(GoogleTest)

set(TEST_EXE_NAME test_platform_adaptive_strings)

add_executable(${TEST_EXE_NAME}
    test_platform_adaptive_strings.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_platform_adaptive_strings
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
