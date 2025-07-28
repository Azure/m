cmake_minimum_required(VERSION 3.23)


if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_memory
        test_memory_unique_span.cpp
        test_raw_allocator.cpp
    )

    target_link_libraries(test_memory
        m_memory
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_memory)

    list(APPEND m_installation_targets
    )

    set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

endif()

