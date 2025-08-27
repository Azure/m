cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_test_data
        test_test_data.cpp
    )

    target_compile_features(test_test_data PUBLIC cxx_std_23)

    target_link_libraries(test_test_data
        m_test_data
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_test_data)
endif()
