cmake_minimum_required(VERSION 3.23)

add_executable(pe2l
    main.cpp "traverse.cpp" "traverse.h")

target_compile_definitions(pe2l PUBLIC _CRT_SECURE_NO_WARNINGS)

target_link_libraries(pe2l
    m_byte_streams
    m_command_options
    m_csv
    m_filesystem
    m_pe
)

set_target_properties(pe2l PROPERTIES
  VERSION ${VERSION}  # ${VERSION} was defined in the main CMakeLists.
)

#enable_testing()

#add_test(
#    NAME TestPe2lFirst
#    COMMAND pe2l try-loader ${RUNTIME_OUTPUT_NAME}
#)