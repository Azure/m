cmake_minimum_required(VERSION 3.23)

target_sources(m_utf PUBLIC FILE_SET HEADERS FILES 
    m/utf/decode_result.h 
    m/utf/exceptions.h 
    m/utf/utf_decode.h 
    m/utf/utf_encode.h 
)
