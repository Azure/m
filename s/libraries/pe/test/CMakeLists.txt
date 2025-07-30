cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_pe
      test_pe.cpp
    )

    target_link_libraries(
      test_pe
      m_byte_streams
      m_filesystem
      m_math
      m_memory
      m_pe
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_pe)
endif()
