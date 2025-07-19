cmake_minimum_required(VERSION 3.23)

target_sources(m_html_writer PRIVATE
    html_writer.cpp
)

target_include_directories(m_html_writer PUBLIC
    ../include
)

target_link_libraries(m_html_writer PUBLIC
    m_io
    m_math
    m_utf
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
