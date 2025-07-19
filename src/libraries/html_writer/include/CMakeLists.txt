cmake_minimum_required(VERSION 3.23)

target_sources(m_html_writer PUBLIC FILE_SET HEADERS FILES
    m/html_writer/html_writer.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
