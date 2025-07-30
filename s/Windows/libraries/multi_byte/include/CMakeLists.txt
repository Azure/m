cmake_minimum_required(VERSION 3.23)

target_sources(m_multi_byte PUBLIC FILE_SET HEADERS FILES
    m/multi_byte/code_page.h
    m/multi_byte/convert.h
    m/multi_byte/convert_acp.h
    m/multi_byte/convert_wchar.h
)
