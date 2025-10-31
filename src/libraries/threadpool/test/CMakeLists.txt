cmake_minimum_required(VERSION 3.23)

if (WIN32)
    include(GoogleTest)

    add_executable(test_threadpool
        test_periodic_timer.cpp
        test_timer.cpp
        test_work_queue.cpp
    )

    target_compile_features(test_threadpool PUBLIC cxx_std_23)

    target_link_libraries(
        test_threadpool
        m_threadpool
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_threadpool)
endif()
