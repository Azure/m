cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_utility
        test_pointers.cpp
    )

    target_compile_features(test_utility PUBLIC ${M_CXX_STD})

    target_link_libraries(test_utility
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_utility)

    list(APPEND m_installation_targets
    )

    set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

endif()
