cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(
    test_math
    add_unsigned_unsigned_to_unsigned.cpp
    add_unsigned_signed_to_unsigned.cpp
    safe_integers_addition_functor.cpp 
    exercise_negation.cpp
)

target_compile_features(test_math PUBLIC ${M_CXX_STD})

target_link_libraries(
    test_math
    m_math
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
