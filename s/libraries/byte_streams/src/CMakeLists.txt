cmake_minimum_required(VERSION 3.23)

target_sources(m_byte_streams PRIVATE
    byte_streams.cpp
    memory_stream.cpp
)

target_link_libraries(m_byte_streams PUBLIC
    m_cast
    m_io
    m_math
)

target_include_directories(m_byte_streams PUBLIC
    ../include
)
