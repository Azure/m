cmake_minimum_required(VERSION 3.23)

target_sources(m_chrono PRIVATE
    chrono.cpp
)

target_link_libraries(m_chrono PUBLIC
    m_wrappers
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
