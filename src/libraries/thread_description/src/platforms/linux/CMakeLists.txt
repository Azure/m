cmake_minimum_required(VERSION 3.23)

target_sources(m_thread_description PRIVATE
    thread_description_impl.cpp
)

target_link_libraries(m_thread_description PUBLIC
)

target_include_directories(m_thread_description PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
