cmake_minimum_required(VERSION 3.30)

# Set the name of your project here
project(test_pe)

include(GoogleTest)

add_executable(
  test_pe
  test_pe.cpp
)

target_link_libraries(
  test_pe
  byte_streams
  filesystem
  math
  memory
  pe
  GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_pe)
