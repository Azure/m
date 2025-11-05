cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_chonk)

add_executable(${TEST_EXE_NAME}
    test_chonk.cpp
)

target_link_libraries(${TEST_EXE_NAME}
    m_chonk
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})
