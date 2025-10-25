cmake_minimum_required(VERSION 3.23)

target_sources(m_string_buffer PUBLIC FILE_SET HEADERS FILES 
    m/string_buffer/string_buffer.h
    m/string_buffer/string_buffer_base.h
    m/string_buffer/string_buffer_overflow.h
    m/string_buffer/string_buffer_overflow_provider.h
    m/string_buffer/string_buffer_pooled_overflow.h
    m/string_buffer/try_cast_basic_sstring.h
)
