cmake_minimum_required(VERSION 3.23)

target_sources(m_chrono PUBLIC FILE_SET HEADERS FILES
    m/chrono/windows_chrono_casts.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
