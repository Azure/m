cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
    pcwstr.cpp
    registry_win32_key_keyoperations.cpp
    registry_win32_key_valueoperations.cpp
    win32_security_attributes.cpp
)

target_link_libraries(m_pil PUBLIC
    m_debugging
    m_errors
    m_formatters
    m_threadpool
)

target_include_directories(m_pil PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
