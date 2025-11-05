cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_memory
    test_memory_unique_span.cpp
    test_raw_allocator.cpp
)

target_compile_features(test_memory PUBLIC cxx_std_23)

target_link_libraries(test_memory
    m_memory
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

list(APPEND m_installation_targets
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

