cmake_minimum_required(VERSION 3.23)

target_sources(m_random PRIVATE
    random.cpp
)

target_include_directories(m_random PUBLIC
    ../include
)

target_link_libraries(m_random PUBLIC
    m_cast
    m_memory
)


set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
