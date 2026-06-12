cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_tracing)

add_executable(${TEST_EXE_NAME}
    exercise_tracing.cpp
    test_fast_sink.cpp
    test_monitor_teardown.cpp
    test_slow_sink.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_cast
    m_math
    m_strings
    m_tracing
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
