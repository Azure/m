cmake_minimum_required(VERSION 3.23)

target_include_directories(m_pil PUBLIC
    ../include
)

target_sources(m_pil PRIVATE
    pil.cpp
    platform.cpp
    registry.cpp
    registry_base_types.cpp
    registry_key.cpp
    registry_monitor.cpp
    registry_path.cpp
)

add_subdirectory(buffered)
add_subdirectory(direct)
add_subdirectory(passthrough)

target_compile_definitions(m_pil PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_pil PUBLIC
    m_cast
    m_chrono
    m_error_handling
    m_math
    m_strings
    m_tracing
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
