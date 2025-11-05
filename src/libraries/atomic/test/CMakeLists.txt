cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_atomic)

add_executable(${TEST_EXE_NAME}
    test_atomic.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC cxx_std_23)

target_link_libraries(${TEST_EXE_NAME}
    m_atomic
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
