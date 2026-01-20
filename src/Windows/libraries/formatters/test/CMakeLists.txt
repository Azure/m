cmake_minimum_required(VERSION 3.23)

include(GoogleTest)

set(TEST_EXE_NAME test_formatters)

add_executable(${TEST_EXE_NAME}
    test_FILETIME.cpp
    test_FILE_ACTION.cpp
    test_FILE_NOTIFY_EXTENDED_INFORMATION.cpp
    test_FILE_NOTIFY_INFORMATION.cpp
    test_HKEY.cpp
    test_HRESULT.cpp
    test_NTSTATUS.cpp
    test_OVERLAPPED.cpp
    test_Win32ErrorCode.cpp
 )

target_compile_features(${TEST_EXE_NAME} PUBLIC ${M_CXX_STD})

target_link_libraries(
    ${TEST_EXE_NAME}
    m_formatters
    GTest::gtest_main
)

enable_testing()

if(DISCOVER_TESTS)
    gtest_discover_tests(${TEST_EXE_NAME})
endif()
