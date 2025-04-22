cmake_minimum_required(VERSION 3.30)

if(M_BUILD_TESTS)
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
endif()
