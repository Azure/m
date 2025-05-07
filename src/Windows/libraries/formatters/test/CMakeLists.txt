cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_formatters
        test_FILETIME.cpp
        test_FILE_ACTION.cpp
        test_FILE_NOTIFY_EXTENDED_INFORMATION.cpp
        test_FILE_NOTIFY_INFORMATION.cpp
        test_NTSTATUS.cpp
        test_OVERLAPPED.cpp
        test_Win32ErrorCode.cpp
    )

    target_link_libraries(
        test_formatters
        m_formatters
        GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_formatters)
endif()
