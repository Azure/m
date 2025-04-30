cmake_minimum_required(VERSION 3.23)

project(m)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_utf
      test_non_shortest_utf8.cpp
      test_truncated_utf8.cpp
      utf16_typed_decoder.cpp
      utf16be_byte_decoder.cpp
      utf16le_byte_decoder.cpp
      utf32_byte_decoder.cpp
      utf32_typed_decoder.cpp
      utf8_byte_decoder.cpp
      utf8_typed_decoder.cpp
    )

    target_link_libraries(test_utf
      m_cast
      m_strings
      m_math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_utf)
endif()
