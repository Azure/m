cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_filesystem
    exercise_monitor.cpp
    exercise_path_casts.cpp
    exercise_path_formatting.cpp
    exercise_store.cpp
)

target_compile_features(test_filesystem PUBLIC ${M_CXX_STD})

add_executable(stress_monitor
    stress_monitor.cpp
)

target_compile_features(stress_monitor PUBLIC ${M_CXX_STD})

add_executable(stress_load_store
    stress_load_store.cpp
)

target_compile_features(stress_load_store PUBLIC ${M_CXX_STD})

target_link_libraries(test_filesystem
    m_chrono
    m_debugging
    m_filesystem
    m_googletest
    m_googletest_main
    m_print
    GTest::gtest_main
)

target_link_libraries(stress_monitor
    m_chrono
    m_debugging
    m_filesystem
    m_googletest
    m_print
    GTest::gtest_main
)

target_link_libraries(stress_load_store
    m_chrono
    m_debugging
    m_filesystem
    m_googletest
    m_random
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(test_filesystem stress_monitor)
endif()

list(APPEND m_installation_targets
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

