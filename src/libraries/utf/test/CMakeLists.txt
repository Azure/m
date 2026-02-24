cmake_minimum_required(VERSION 3.23)

project(m)

include(GoogleTest)

set(TEST_EXE_NAME test_utf)

add_executable(${TEST_EXE_NAME}
    test_non_shortest_utf8.cpp
    test_truncated_utf8.cpp
    test_utf_extras.cpp
    utf16_typed_decoder.cpp
    utf16be_byte_decoder.cpp
    utf16le_byte_decoder.cpp
    utf32_byte_decoder.cpp
    utf32_typed_decoder.cpp
    utf8_byte_decoder.cpp
    utf8_typed_decoder.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_cast
    m_strings
    m_math
    m_test_data
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
