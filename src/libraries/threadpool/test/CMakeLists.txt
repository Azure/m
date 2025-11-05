cmake_minimum_required(VERSION 3.23)

if (WIN32)
    include(GoogleTest)

    set(TEST_EXE_NAME test_threadpool)

    add_executable(${TEST_EXE_NAME}
        test_periodic_timer.cpp
        test_timer.cpp
        test_work_queue.cpp
    )

    target_compile_features(${TEST_EXE_NAME} PUBLIC cxx_std_23)

    target_link_libraries(
        ${TEST_EXE_NAME}
        m_threadpool
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    if(DISCOVER_TESTS)
        gtest_discover_tests(${TEST_EXE_NAME})
    endif()
endif()
