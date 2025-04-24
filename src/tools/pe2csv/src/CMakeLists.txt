cmake_minimum_required(VERSION 3.23)

add_executable(pe2csv
    main.cpp)

target_compile_definitions(pe2csv PUBLIC _CRT_SECURE_NO_WARNINGS)

target_link_libraries(pe2csv
    m_byte_streams
    m_command_options
    m_csv
    m_filesystem
    m_pe
)
