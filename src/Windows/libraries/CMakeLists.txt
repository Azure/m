cmake_minimum_required(VERSION 3.23)

add_subdirectory(debugging)
add_subdirectory(errors)
add_subdirectory(formatters)
add_subdirectory(multi_byte)
add_subdirectory(windows_strings)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
