cmake_minimum_required(VERSION 3.23)

target_include_directories(m_pil PUBLIC
    ../include
)

target_sources(m_pil PRIVATE
    buffered_platform.cpp
    buffered_registry.cpp
    buffered_registry_key_key_operations.cpp
    buffered_registry_key_value_operations.cpp
    pil.cpp
    platform.cpp
    registry.cpp
    registry_base_types.cpp
    registry_key.cpp
    registry_passthrough.cpp
)

target_compile_definitions(m_pil PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_pil PUBLIC
    m_cast
    m_math
    m_strings
    m_tracing
)

if(WIN32)
    target_link_libraries(m_pil PUBLIC
        m_windows_strings
    )
elseif(1)
    target_link_libraries(m_pil PUBLIC
        m_linux_strings
    )
endif()

# Include platform-specific code
add_subdirectory(Platforms)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
