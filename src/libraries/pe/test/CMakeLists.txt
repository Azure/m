cmake_minimum_required(VERSION 3.30)

if(M_BUILD_TESTS)
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
endif()
