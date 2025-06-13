cmake_minimum_required(VERSION 3.23)

if (WIN32)
    if(M_BUILD_TESTS)
        include(GoogleTest)

        add_executable(test_thread_description
            test_thread_description.cpp
        )

        target_link_libraries(
            test_thread_description
            m_thread_description
            m_formatters
            GTest::gtest_main
        )

        enable_testing()

        gtest_discover_tests(test_thread_description)
    endif()
endif()
