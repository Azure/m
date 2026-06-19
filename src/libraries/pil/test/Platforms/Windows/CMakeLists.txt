cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

find_package(pugixml CONFIG REQUIRED)

set(TEST_EXE_NAME test_win32_registry)

add_executable(${TEST_EXE_NAME}
    test_buffered_over_direct_registry.cpp
    test_buffered_capture.cpp
    test_buffered_create_key.cpp
    test_buffered_filesystem.cpp
    test_buffered_fs_mock.cpp
    test_buffered_mock.cpp
    test_buffered_save.cpp
    test_direct_registry.cpp
    test_direct_filesystem.cpp
    test_passthrough_filesystem.cpp
    test_redirecting_filesystem.cpp
    test_fault.cpp
    test_fault_public.cpp
    test_fault_filesystem.cpp
    test_intercepting_webcore.cpp
    test_journaling.cpp
    test_journaling_filesystem.cpp
    test_logging_float.cpp
    test_logging_filesystem.cpp
    test_materializing_webcore.cpp
    test_registry_monitoring.cpp
    test_filesystem_monitoring.cpp
    "test_logging_registry.cpp")

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_include_directories(${TEST_EXE_NAME} PRIVATE
    ../../../src
)

target_link_libraries(${TEST_EXE_NAME}
    m_filesystem
    m_math
    m_memory
    m_multi_byte
    m_pil
    pugixml::pugixml
    m_googletest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
