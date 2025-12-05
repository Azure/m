cmake_minimum_required(VERSION 3.23)

# This test is having a hard time building since moving to not forcing the compiler
# version to c++23

include(GoogleTest)

set(TEST_EXE_NAME test_strings)

add_executable(${TEST_EXE_NAME}
    compare_optional_wstring_view.cpp
    punning_test.cpp
    test_convert.cpp
    test_literal_string_view.cpp
    verify_to_string.cpp
    verify_to_u8string.cpp
    verify_to_u16string.cpp
    verify_to_u32string.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_cast
    m_math
    m_platform_adaptive_strings
    m_strings
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
