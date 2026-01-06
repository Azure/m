cmake_minimum_required(VERSION 3.23)

add_subdirectory(errors)
add_subdirectory(formatters)
add_subdirectory(multi_byte)
add_subdirectory(mwin32)
add_subdirectory(win32)
add_subdirectory(windows_strings)
add_subdirectory(windows_wrappers)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
