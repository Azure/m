cmake_minimum_required(VERSION 3.23)

if (WIN32)
if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_threadpool
        test_timer.cpp
    )

    target_link_libraries(
        test_threadpool
        m_threadpool
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_threadpool)
endif()
endif()
