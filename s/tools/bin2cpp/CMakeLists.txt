cmake_minimum_required(VERSION 3.23)

add_subdirectory(src)
#add_subdirectory(doc)
#add_subdirectory(test)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
