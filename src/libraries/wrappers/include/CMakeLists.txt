cmake_minimum_required(VERSION 3.23)

target_sources(m_wrappers PUBLIC FILE_SET HEADERS FILES
    m/wrappers/wrapper_template.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
