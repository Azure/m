cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)

    include(GoogleTest)

    add_executable(
      test_csv
      test_field_quoter.cpp 
      test_row_writer.cpp)

    target_link_libraries(
      test_csv
      m_csv
      m_math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_csv)
endif()
