cmake_minimum_required(VERSION 3.23)

find_package(pugixml CONFIG REQUIRED)

target_sources(m_pil PRIVATE
    directory_mutation_operations.cpp
    directory_read_operations.cpp
    filesystem.cpp
    filesystem_monitor.cpp
    filesystem_serialization.cpp
    platform.cpp
    registry.cpp
    registry_key_key_operations.cpp
    registry_key_value_operations.cpp
    registry_monitor.cpp
)

target_link_libraries(m_pil PUBLIC
    m_debugging
    m_sstring
    m_threadpool
    pugixml::pugixml
)

target_include_directories(m_pil PRIVATE
    ../../../
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
