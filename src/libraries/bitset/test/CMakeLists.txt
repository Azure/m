cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_bitset
    test_bitset.cpp
)

target_compile_features(test_bitset PUBLIC cxx_std_23)

target_link_libraries(test_bitset
    m_bitset
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_bitset)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
