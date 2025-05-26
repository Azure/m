cmake_minimum_required(VERSION 3.23)

target_sources(m_googletest PUBLIC FILE_SET HEADERS FILES 
    m/googletest/onexecutionend.h 
    m/googletest/temporary_directory.h
)
