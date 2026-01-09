cmake_minimum_required(VERSION 3.23)

target_sources(m_multi_byte PUBLIC FILE_SET HEADERS FILES
    m/multi_byte/code_page.h
    m/multi_byte/convert.h
    m/multi_byte/multi_byte_to_utf16.h
    m/multi_byte/to_span.h
    m/multi_byte/utf16_to_multi_byte.h
)
