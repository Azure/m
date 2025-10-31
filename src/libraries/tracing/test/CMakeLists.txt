cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_tracing
    exercise_tracing.cpp
    test_fast_sink.cpp
    test_slow_sink.cpp
)

target_compile_features(test_tracing PUBLIC ${M_CXX_STD})

target_link_libraries(test_tracing
    m_cast
    m_math
    m_strings
    m_tracing
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_tracing)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
