cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_mc)

add_executable(${TEST_EXE_NAME}
    test_queue.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_mc
    m_print
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

list(APPEND m_installation_targets
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

