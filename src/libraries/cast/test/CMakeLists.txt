cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_cast
      test_cast.cpp
      test_try_cast.cpp 
      to.cpp
    )

    target_compile_features(test_cast PUBLIC cxx_std_23)

    target_link_libraries(
      test_cast
      m_cast
      m_math
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_cast)
endif()
