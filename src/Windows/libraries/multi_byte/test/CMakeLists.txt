cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_multi_byte)

add_executable(${TEST_EXE_NAME}
    test_multi_byte_to_utf16.cpp
    test_utf16_to_multi_byte.cpp
    wchar_decoder.cpp
    verify_mb_err_invalid_chars.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_cast
    m_math
    m_multi_byte
    m_sstring
    m_strings
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
