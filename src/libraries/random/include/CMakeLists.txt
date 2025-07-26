cmake_minimum_required(VERSION 3.23)

target_sources(m_random PUBLIC FILE_SET HEADERS FILES 
    m/random/random.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
