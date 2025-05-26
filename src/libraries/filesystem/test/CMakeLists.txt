cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_filesystem
        exercise_monitor.cpp
        exercise_path_casts.cpp
        exercise_path_formatting.cpp
        exercise_store.cpp
    )

    target_link_libraries(test_filesystem
        m_filesystem
        m_googletest
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_filesystem)

    list(APPEND m_installation_targets
        test_filesystem
    )

    set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

endif()
