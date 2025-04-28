cmake_minimum_required(VERSION 3.23)

project(m)

if(M_BUILD_TESTS)
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
      m_cast
      m_strings
      m_math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_utf)
endif()
