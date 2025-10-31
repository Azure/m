cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_puddle
    test_puddle.cpp
)

target_compile_features(test_puddle PUBLIC ${M_CXX_STD})

target_link_libraries(test_puddle
    m_inplace_vector
    m_puddle
    m_print
    m_test_data
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_puddle)
