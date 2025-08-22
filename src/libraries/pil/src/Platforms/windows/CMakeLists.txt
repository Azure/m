cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
    platform.cpp
    pcwstr.cpp
    win32_platform.cpp
    win32_registry.cpp
    win32_registry_key_key_operations.cpp
    win32_registry_key_value_operations.cpp
    win32_registry_monitor.cpp
    win32_registry_monitor_token.cpp
    win32_security_attributes.cpp
)

target_link_libraries(m_pil PUBLIC
    m_debugging
    m_errors
    m_formatters
    m_sstring
    m_threadpool
    m_win32
)

target_include_directories(m_pil PRIVATE
    ../../
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
