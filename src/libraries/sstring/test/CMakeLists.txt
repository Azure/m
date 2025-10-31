cmake_minimum_required(VERSION 3.23)

    include(GoogleTest)

    add_executable(test_sstring
        test_sstring.cpp
    )

    target_compile_features(test_sstring PUBLIC ${M_CXX_STD})

    target_link_libraries(test_sstring
        m_sstring
        m_test_data
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_sstring)
