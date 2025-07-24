cmake_minimum_required(VERSION 3.23)

target_sources(m_chrono PRIVATE
    chrono.cpp
)

add_subdirectory(Platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
