cmake_minimum_required(VERSION 3.23)

target_sources(m_utf PUBLIC FILE_SET HEADERS FILES 
    m/utf/decode.h
    m/utf/decode_iterator.h
    m/utf/decode_result.h
    m/utf/encode.h
    m/utf/exceptions.h
    m/utf/transcode.h
)
