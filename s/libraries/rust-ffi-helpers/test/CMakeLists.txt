cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(test_rust_ffi_helpers
        test_arc_ptr.cpp
     "test_string_in.cpp")

    target_link_libraries(
      test_rust_ffi_helpers
      m_rust_ffi_helpers
      m_cast
      m_strings
      m_math
      GTest::gtest_main
    )

if(WIN32)
    target_link_libraries(test_rust_ffi_helpers
        m_windows_strings
    )
else()
    target_link_libraries(test_rust_ffi_helpers
        m_linux_strings
    )
endif()

    enable_testing()

    gtest_discover_tests(test_rust_ffi_helpers)
endif()