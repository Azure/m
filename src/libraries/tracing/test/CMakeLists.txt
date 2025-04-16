cmake_minimum_required(VERSION 3.30)

include(GoogleTest)

add_executable(
  test_tracing
  exercise_tracing.cpp
)

target_link_libraries(
  test_tracing
  cast
  math
  strings
  tracing
  GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_tracing)
