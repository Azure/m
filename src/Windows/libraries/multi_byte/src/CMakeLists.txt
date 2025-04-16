cmake_minimum_required(VERSION 3.30)

add_library(multi_byte STATIC
    "multi_byte_to_utf16.cpp"
  "utf16_to_multi_byte.cpp")

target_include_directories(multi_byte PUBLIC
    ../include
)

target_link_libraries(multi_byte PUBLIC
    errors
    math
)

set_target_properties(multi_byte PROPERTIES
    VERSION ${VERSION}
)

