cmake_minimum_required(VERSION 3.23)

target_sources(m_debugging PRIVATE
    dbg_format.cpp
)

target_include_directories(m_debugging PUBLIC
    ../include
)

target_link_libraries(m_debugging PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
