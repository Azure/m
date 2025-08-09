cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_win32_registry
        test_buffered_over_direct_registry.cpp
        test_direct_registry.cpp
    )

    target_compile_features(test_win32_registry PUBLIC cxx_std_23)

    target_link_libraries(test_win32_registry
        m_filesystem
        m_math
        m_memory
        m_multi_byte
        m_pil
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_win32_registry)
endif()
