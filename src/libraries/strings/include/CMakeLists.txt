cmake_minimum_required(VERSION 3.23)

target_sources(m_strings PUBLIC FILE_SET HEADERS FILES 
    m/strings/convert.h 
    m/strings/literal_string_view.h 
    m/strings/ordinal_compare.h 
    m/strings/punning.h
    m/strings/static_string.h 
)
