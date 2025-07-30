cmake_minimum_required(VERSION 3.23)

target_sources(m_memory PUBLIC FILE_SET HEADERS FILES 
    m/memory/memory.h
)

add_subdirectory(platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
