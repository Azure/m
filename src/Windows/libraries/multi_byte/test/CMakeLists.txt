cmake_minimum_required(VERSION 3.30)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_multi_byte
      "test_multi_byte_to_utf16.cpp"
      "test_utf16_to_multi_byte.cpp" "test_acp_apis.cpp")

    target_link_libraries(
      test_multi_byte
      cast
      multi_byte
      strings
      math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_multi_byte)
endif()
