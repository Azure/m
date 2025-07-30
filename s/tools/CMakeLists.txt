cmake_minimum_required(VERSION 3.23)

add_subdirectory(bin2cpp)
add_subdirectory(helloworld)
add_subdirectory(pe2l)
add_subdirectory(pe2csv)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
