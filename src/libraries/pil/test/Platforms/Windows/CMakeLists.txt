cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_win32_registry)

add_executable(${TEST_EXE_NAME}
    test_buffered_over_direct_registry.cpp
    test_direct_registry.cpp
    test_registry_monitoring.cpp
    "test_logging_registry.cpp")

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_filesystem
    m_math
    m_memory
    m_multi_byte
    m_pil
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
