cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_googletest
    exercise_temporary_directory.cpp
)

target_compile_features(test_googletest PUBLIC ${M_CXX_STD})

target_link_libraries(
    test_googletest
    m_googletest
    m_strings
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_googletest)

