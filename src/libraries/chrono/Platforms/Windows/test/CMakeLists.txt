cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(
    test_windows_chrono_casts
    test_windows_chrono_casts.cpp
)

target_compile_features(test_windows_chrono_casts PUBLIC ${M_CXX_STD})

target_link_libraries(test_windows_chrono_casts
    m_chrono
    m_formatters
    m_windows_chrono
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_windows_chrono_casts)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
