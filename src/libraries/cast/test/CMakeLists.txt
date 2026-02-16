cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_cast)

add_executable(
    ${TEST_EXE_NAME}
    test_cast.cpp
    test_try_cast.cpp 
    test_to_edge_cases.cpp
    to.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_cast
    m_math
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
