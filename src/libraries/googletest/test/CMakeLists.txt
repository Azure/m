cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_googletest)

add_executable(${TEST_EXE_NAME}
    exercise_temporary_directory.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_googletest
    m_strings
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

