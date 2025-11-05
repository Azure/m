cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(
    test_csv
    test_field_quoter.cpp
    test_row_writer.cpp
    test_csv_breaker.cpp
)

target_compile_features(test_csv PUBLIC ${M_CXX_STD})

target_link_libraries(
    test_csv
    m_csv
    m_math
    m_string_buffer
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_csv)
