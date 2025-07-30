cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
    direct_platform.cpp
    pcwstr.cpp
    platform_win32.cpp
    registry_win32.cpp
    registry_win32_key_key_operations.cpp
    registry_win32_key_value_operations.cpp
    win32_security_attributes.cpp
)

target_link_libraries(m_pil PUBLIC
    m_debugging
    m_errors
    m_formatters
    m_threadpool
)

target_include_directories(m_pil PRIVATE
    ../../
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
