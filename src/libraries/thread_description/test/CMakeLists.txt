cmake_minimum_required(VERSION 3.23)

if (WIN32)
    include(GoogleTest)

    set(TEST_EXE_NAME test_thread_description)

    add_executable(${TEST_EXE_NAME}
        test_thread_description.cpp
    )

    target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

    target_link_libraries(
        ${TEST_EXE_NAME}
        m_thread_description
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    if(DISCOVER_TESTS)
        gtest_discover_tests(${TEST_EXE_NAME})
    endif()
endif()

