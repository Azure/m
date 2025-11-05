cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_arc_ptr)

add_executable(${TEST_EXE_NAME}
    test_arefc_ptr.cpp
)

target_compile_features(test_arc_ptr PUBLIC ${M_CXX_STD})

target_link_libraries(test_arc_ptr
    m_arefc_ptr
    m_print
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
