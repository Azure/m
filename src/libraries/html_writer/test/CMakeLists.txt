cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)

    include(GoogleTest)

    add_executable(test_html_writer
        test_html_writer.cpp 
)

    target_link_libraries(test_html_writer
        m_html_writer
        m_math
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_html_writer)
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
