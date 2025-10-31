cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_linux_strings
    compare.cpp
)

target_compile_features(test_linux_strings PUBLIC ${M_CXX_STD})

target_link_libraries(test_linux_strings
    m_linux_strings        
    m_cast
    m_math
    m_strings
    m_tracing
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_linux_strings)
