cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_chonk
    test_chonk.cpp
)

target_link_libraries(test_chonk
    m_chonk
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_chonk)
