cmake_minimum_required(VERSION 3.23)

target_sources(m_win32 PUBLIC FILE_SET HEADERS FILES
    m/win32/event.h
    m/win32/handle.h
    m/win32/registry.h
    m/win32/threadpool.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
