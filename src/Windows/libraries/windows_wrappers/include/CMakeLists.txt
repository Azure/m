cmake_minimum_required(VERSION 3.23)

target_sources(m_windows_wrappers PUBLIC FILE_SET HEADERS FILES
    m/windows_wrappers/win32_dword_ms.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
