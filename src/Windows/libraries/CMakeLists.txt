cmake_minimum_required(VERSION 3.23)

add_subdirectory(errors)
add_subdirectory(multi_byte)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
