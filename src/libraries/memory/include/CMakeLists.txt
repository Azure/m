cmake_minimum_required(VERSION 3.23)

target_sources(m_memory PUBLIC FILE_SET HEADERS FILES 
    m/memory/aligned_allocation.h
    m/memory/memory.h
    m/memory/raw_allocation_helper.h
    m/memory/raw_array_allocator.h
    m/memory/unique_span.h
)

add_subdirectory(platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
