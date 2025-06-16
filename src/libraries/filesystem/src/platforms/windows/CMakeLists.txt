cmake_minimum_required(VERSION 3.23)

target_sources(m_filesystem PRIVATE
    directory_watcher.cpp
    string_to_path_native.cpp
)

target_link_libraries(m_filesystem PUBLIC
    m_debugging
    m_formatters
    m_threadpool
)

target_include_directories(m_filesystem PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
