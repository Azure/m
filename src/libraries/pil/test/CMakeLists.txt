cmake_minimum_required(VERSION 3.23)

set(TEST_EXE_NAME test_pil_registry)

include(GoogleTest)

add_executable(${TEST_EXE_NAME}
    test_pil_registry.cpp
    test_key_path.cpp
    test_redirecting_redirector.cpp
)

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_include_directories(${TEST_EXE_NAME} PRIVATE
    ../src
)

target_link_libraries(${TEST_EXE_NAME}
    m_filesystem
    m_math
    m_memory
    m_pil
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()

add_subdirectory(Platforms)
