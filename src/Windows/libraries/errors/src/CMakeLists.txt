cmake_minimum_required(VERSION 3.23)

target_sources(m_errors PRIVATE
    errors.cpp
    hresult.cpp
    win32_error_code.cpp
)

target_include_directories(m_errors PUBLIC
    ../include
)

target_link_libraries(m_errors PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
