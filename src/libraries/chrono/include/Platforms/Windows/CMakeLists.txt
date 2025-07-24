cmake_minimum_required(VERSION 3.23)

target_sources(m_chrono PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/.. FILES 
    m/chrono/windows_chrono_casts.h
)
