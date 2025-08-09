cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_windows_chrono_casts
      test_windows_chrono_casts.cpp
    )

    target_compile_features(test_windows_chrono_casts PUBLIC cxx_std_23)

    target_link_libraries(test_windows_chrono_casts
      m_chrono
      m_formatters
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_windows_chrono_casts)
endif()
