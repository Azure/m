cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_strings
        punning_test.cpp
      test_convert.cpp
      test_literal_string_view.cpp
      verify_to_string.cpp
      verify_to_u8string.cpp
      verify_to_u16string.cpp
      verify_to_u32string.cpp
    )

    target_link_libraries(
      test_strings
      m_cast
      m_strings
      m_math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_strings)
endif()