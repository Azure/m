cmake_minimum_required(VERSION 3.23)

if(M_BUILD_TESTS)
    include(GoogleTest)

    add_executable(
      test_pil_registry
      test_pil_registry.cpp
    )

    target_link_libraries(
      test_pil_registry
      m_filesystem
      m_math
      m_memory
      m_pil
      GTest::gtest_main
    )

    enable_testing()

    gtest_discover_tests(test_pil_registry)
endif()

add_subdirectory(Platforms)
