cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_win32
        test_registry_helpers.cpp
        test_win32_threadpool.cpp
    )

    target_compile_features(test_win32 PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_win32
        m_win32
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_win32)
endif()
