cmake_minimum_required(VERSION 3.23)

target_sources(m_mc PUBLIC FILE_SET HEADERS FILES 
    mc/iterators.h
    mc/queue.h 
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

