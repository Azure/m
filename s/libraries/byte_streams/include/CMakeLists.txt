cmake_minimum_required(VERSION 3.23)

target_sources(m_byte_streams PUBLIC FILE_SET HEADERS FILES
    m/byte_streams/byte_streams.h
    m/byte_streams/memory_based_byte_streams.h
)
