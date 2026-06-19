cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
    journal.cpp
    journal_entries.cpp
    filesystem.cpp
    filesystem_journal_entries.cpp
    platform.cpp
    registry.cpp
    registry_key.cpp
    replay.cpp
)

target_link_libraries(m_pil PUBLIC
)

target_include_directories(m_pil PRIVATE
    ../../../
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
