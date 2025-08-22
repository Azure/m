cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_print
        test_print.cpp
    )

    target_compile_features(test_print PUBLIC ${M_CXX_STD})

    target_link_libraries(test_print
        m_print
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_print)
endif()
