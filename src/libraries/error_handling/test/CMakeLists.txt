cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_error_handling
        test_error_handling.cpp
    )

    target_link_libraries(test_error_handling
        m_error_handling
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_error_handling)
endif()
