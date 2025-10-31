cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_arc_ptr
    test_arefc_ptr.cpp
)

target_compile_features(test_arc_ptr PUBLIC ${M_CXX_STD})

target_link_libraries(test_arc_ptr
    m_arefc_ptr
    m_print
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_arc_ptr)
