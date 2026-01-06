cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_mwin32
        test_handle_table.cpp
        test_mwinreg_open_close.cpp
    )

    target_compile_features(test_mwin32 PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32
        m_mwin32
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_mwin32)
endif()
