cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_windows_wrappers
        test_win32_dword_ms.cpp
    )

    target_compile_features(test_windows_wrappers PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_windows_wrappers
        m_wrappers
        m_windows_wrappers
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_windows_wrappers)
endif()
