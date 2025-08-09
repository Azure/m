cmake_minimum_required(VERSION 3.23)

target_sources(m_wrappers PRIVATE
    wrappers.cpp
)

target_include_directories(m_wrappers PUBLIC
    ../include
)

target_link_libraries(m_wrappers PUBLIC
    m_cast
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
