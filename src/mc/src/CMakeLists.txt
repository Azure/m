cmake_minimum_required(VERSION 3.23)

target_sources(m_mc PRIVATE
    queue.cpp
)

target_include_directories(m_mc PUBLIC
    ../include
)

target_link_libraries(m_mc PUBLIC
)


set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
