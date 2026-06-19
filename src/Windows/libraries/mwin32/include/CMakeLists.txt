cmake_minimum_required(VERSION 3.23)

target_sources(m_mwin32 PUBLIC FILE_SET HEADERS FILES
    m/mwin32/mWindows.h
    m/mwin32/mwinreg.h
    m/mwin32/mwinfile.h
    m/mwin32/mwinhwc.h
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
