cmake_minimum_required(VERSION 3.30)

# Set the name of your project here
project(test_csv)

include(GoogleTest)

add_executable(
  test_csv
  test_field_quoter.cpp 
  test_row_writer.cpp)

target_link_libraries(
  test_csv
  csv
  math
  GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_csv)
