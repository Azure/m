cmake_minimum_required(VERSION 3.23)

if(0)
    include(GoogleTest)

    add_executable(test_windows_strings
    )

    target_compile_features(test_windows_strings PUBLIC ${M_CXX_STD})

    target_link_libraries(test_windows_strings
        m_windows_strings        
        m_cast
        m_math
        m_strings
        m_tracing
        GTest::gtest_main
    )

    enable_testing()

    if(DISCOVER_TESTS)
        gtest_discover_tests(${TEST_EXE_NAME})
    endif()
endif()