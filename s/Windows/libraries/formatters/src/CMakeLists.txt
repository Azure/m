cmake_minimum_required(VERSION 3.23)

target_sources(m_formatters PRIVATE
    formatters.cpp
)

target_include_directories(m_formatters PUBLIC
    ../include
)

target_link_libraries(m_formatters PUBLIC
    m_cast
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
