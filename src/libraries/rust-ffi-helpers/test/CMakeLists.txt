cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_rust_ffi_helpers)

add_executable(${TEST_EXE_NAME}
    test_arc_ptr.cpp
    test_string_in.cpp)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_rust_ffi_helpers
    m_cast
    m_strings
    m_math
    GTest::gtest_main
)

if(WIN32)
target_link_libraries(${TEST_EXE_NAME}
    m_windows_strings
)
else()
target_link_libraries(${TEST_EXE_NAME}
    m_linux_strings
)
endif()

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

