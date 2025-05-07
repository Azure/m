cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
        test_rfc3339_clock
        exercise_clock.cpp
    )

    target_link_libraries(
        test_rfc3339_clock
        m_rfc3339_clock
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_rfc3339_clock)
endif()
