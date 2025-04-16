cmake_minimum_required(VERSION 3.30)

include(GoogleTest)

add_executable(
  test_utf
  test_cvt_u32_to_u8.cpp
  test_cvt_u8_to_u32.cpp
  test_truncated_utf8.cpp
  test_utf16_encode.cpp
  test_utf32_encode.cpp
  test_utf8_decode.cpp
  test_non_shortest_utf8.cpp)

target_link_libraries(
  test_utf
  cast
  strings
  math
  GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_utf)
