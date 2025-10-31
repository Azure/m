cmake_minimum_required(VERSION 3.23)

if (WIN32)
    include(GoogleTest)

    add_executable(test_thread_description
        test_thread_description.cpp
    )

    target_compile_features(test_thread_description PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_thread_description
        m_thread_description
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_thread_description)
endif()
