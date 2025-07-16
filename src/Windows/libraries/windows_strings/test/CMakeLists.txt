cmake_minimum_required(VERSION 3.23)

if(0)
if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_windows_strings
    )

    target_link_libraries(test_windows_strings
        m_windows_strings        
        m_cast
        m_math
        m_strings
        m_tracing
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_windows_strings)
endif()

endif()