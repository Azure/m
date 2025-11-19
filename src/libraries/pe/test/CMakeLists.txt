cmake_minimum_required(VERSION 3.23)

set(TEST_EXE_NAME test_pe)

include(GoogleTest)

add_executable(
    ${TEST_EXE_NAME}
    test_pe.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_byte_streams
    m_filesystem
    m_math
    m_memory
    m_pe
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
