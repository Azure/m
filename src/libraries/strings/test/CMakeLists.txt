cmake_minimum_required(VERSION 3.30)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_strings
      test_convert.cpp
      test_literal_string_view.cpp
    )

    target_link_libraries(
      test_strings
      cast
      strings
      math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_strings)
endif()