cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_mc
    test_queue.cpp
)

target_compile_features(test_mc PUBLIC ${M_CXX_STD})

target_link_libraries(test_mc
    m_mc
    m_print
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_mc)

list(APPEND m_installation_targets
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

