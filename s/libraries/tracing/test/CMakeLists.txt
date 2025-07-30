cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_tracing
      exercise_tracing.cpp
    )

    target_link_libraries(
      test_tracing
      m_cast
      m_math
      m_strings
      m_tracing
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_tracing)
endif()
