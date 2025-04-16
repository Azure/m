cmake_minimum_required(VERSION 3.30)

add_executable(pe2l
    main.cpp "traverse.cpp" "traverse.h")

target_compile_definitions(pe2l PUBLIC _CRT_SECURE_NO_WARNINGS)

target_link_libraries(pe2l
    byte_streams
    command_options
    csv
    filesystem
    pe
)

set_target_properties(pe2l PROPERTIES
  VERSION ${VERSION}  # ${VERSION} was defined in the main CMakeLists.
)

#enable_testing()

#add_test(
#    NAME TestPe2lFirst
#    COMMAND pe2l try-loader ${RUNTIME_OUTPUT_NAME}
#)