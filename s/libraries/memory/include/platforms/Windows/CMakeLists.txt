cmake_minimum_required(VERSION 3.23)

target_sources(m_memory PUBLIC FILE_SET HEADERS FILES 
    m/memory/memory_local.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
