cmake_minimum_required(VERSION 3.23)

target_sources(m_strings PRIVATE
    case_insensitive_less.cpp
)

target_link_libraries(m_strings PUBLIC
)

target_include_directories(m_strings PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
