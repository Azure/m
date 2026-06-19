cmake_minimum_required(VERSION 3.23)

target_include_directories(m_pil PUBLIC
    ../include
)

target_sources(m_pil PRIVATE
    create_platform.cpp
    fault_interface.cpp
    filesystem.cpp
    filesystem_monitor.cpp
    pil.cpp
    platform.cpp
    registry.cpp
    registry_base_types.cpp
    registry_key.cpp
    registry_monitor.cpp
    key_path.cpp
    file_path.cpp
    webcore.cpp
)

add_subdirectory(buffered)
add_subdirectory(direct)
add_subdirectory(fault)
add_subdirectory(intercepting)
add_subdirectory(journaling)
add_subdirectory(logging)
add_subdirectory(materializing)
add_subdirectory(passthrough)
add_subdirectory(redirecting)

target_compile_definitions(m_pil PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_pil PUBLIC
    m_cast
    m_chrono
    m_error_handling
    m_math
    m_platform_adaptive_strings
    m_strings
    m_tracing
)

if(WIN32)

target_include_directories(m_pil PRIVATE direct/Platforms/windows)

elseif(LINUX)

target_include_directories(m_pil PRIVATE direct/Platforms/Linux)

endif()


set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
