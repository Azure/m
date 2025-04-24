cmake_minimum_required(VERSION 3.23)

target_sources(m_errors PUBLIC FILE_SET HEADERS FILES
    m/errors/errors.h
    m/errors/win32_error_code.h
)
