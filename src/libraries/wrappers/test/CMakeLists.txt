cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_wrappers
    test_template.cpp
)

target_compile_features(test_wrappers PUBLIC ${M_CXX_STD})

target_link_libraries(
    test_wrappers
    m_wrappers
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_wrappers)
