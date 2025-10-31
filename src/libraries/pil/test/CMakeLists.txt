cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

add_executable(test_pil_registry
    test_pil_registry.cpp
    test_key_path.cpp
    test_redirecting_redirector.cpp
)

target_compile_features(test_pil_registry PUBLIC ${M_CXX_STD})

target_include_directories(test_pil_registry PRIVATE
    ../src
)

target_link_libraries(test_pil_registry
    m_filesystem
    m_math
    m_memory
    m_pil
    GTest::gtest_main
)

enable_testing()

gtest_discover_tests(test_pil_registry)


add_subdirectory(Platforms)
