cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(
    test_pe
    test_pe.cpp
)

target_compile_features(test_pe PUBLIC ${M_CXX_STD})

target_link_libraries(
    test_pe
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
