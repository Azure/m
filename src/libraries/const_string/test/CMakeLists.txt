cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_const_string
        test_const_string.cpp
    )

    target_compile_features(test_const_string PUBLIC ${M_CXX_STD})

    target_link_libraries(test_const_string
        m_const_string
        m_test_data
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_const_string)
endif()
