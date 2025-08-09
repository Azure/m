cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_atomic
        test_atomic.cpp
    )

    target_compile_features(test_atomic PUBLIC cxx_std_23)

    target_link_libraries(test_atomic
        m_atomic
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_atomic)
endif()
