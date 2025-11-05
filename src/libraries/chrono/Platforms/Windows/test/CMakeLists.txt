cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_windows_chrono_casts)

add_executable(
    ${TEST_EXE_NAME}
    test_windows_chrono_casts.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(${TEST_EXE_NAME}
    m_chrono
    m_formatters
    m_windows_chrono
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
