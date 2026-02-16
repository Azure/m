cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_math)

add_executable(
    ${TEST_EXE_NAME}
    add_unsigned_unsigned_to_unsigned.cpp
    add_unsigned_signed_to_unsigned.cpp
    safe_integers_addition_functor.cpp 
    exercise_negation.cpp
    signed_signed_to_signed.cpp
    signed_unsigned_to_unsigned.cpp
    test_division.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_math
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
