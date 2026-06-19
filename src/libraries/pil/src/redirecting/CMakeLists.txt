cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
    filesystem.cpp
    filesystem_monitor.cpp
    platform.cpp
    redirector.cpp
    rundown.cpp
    registry.cpp
    registry_key.cpp
    registry_monitor.cpp
    registry_monitor_change_notification_wrapper.cpp
    webcore.cpp
)

target_link_libraries(m_pil PUBLIC
)

target_include_directories(m_pil PRIVATE
    ../../../
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
