cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_string_buffer
        test_pooled_string_buffer.cpp
        test_string_buffer.cpp
    )

    target_compile_features(test_string_buffer PUBLIC ${M_CXX_STD})

    target_link_libraries(test_string_buffer
        m_string_buffer
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_string_buffer)
endif()
