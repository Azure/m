cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_inplace_vector
        test_inplace_vector.cpp
    )

    target_link_libraries(test_inplace_vector
        m_inplace_vector
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_inplace_vector)
endif()
