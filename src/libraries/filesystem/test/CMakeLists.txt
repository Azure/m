cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_filesystem
        exercise_monitor.cpp
        exercise_path_casts.cpp
        exercise_path_formatting.cpp
        exercise_store.cpp
    )

    add_executable(stress_monitor
        stress_monitor.cpp
    )

    target_link_libraries(test_filesystem
        m_debugging
        m_filesystem
        m_googletest
        GTest::gtest_main
    )

    target_link_libraries(stress_monitor
        m_debugging
        m_filesystem
        m_googletest
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_filesystem stress_monitor)

    list(APPEND m_installation_targets
        test_filesystem
        stress_monitor
    )

    set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

endif()
