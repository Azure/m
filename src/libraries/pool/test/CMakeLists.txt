cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_pool
    test_pool.cpp
)

target_compile_features(test_pool PUBLIC ${M_CXX_STD})

target_link_libraries(test_pool
    m_inplace_vector
    m_pool
    m_print
    m_test_data
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_pool)
