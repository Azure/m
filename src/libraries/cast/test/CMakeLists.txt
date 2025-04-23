cmake_minimum_required(VERSION 3.22)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_cast
      test_cast.cpp
      test_try_cast.cpp 
      to.cpp
    )

    target_link_libraries(
      test_cast
      cast
      math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_cast)
endif()
