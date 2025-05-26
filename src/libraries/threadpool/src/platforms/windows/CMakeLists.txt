cmake_minimum_required(VERSION 3.23)

target_sources(m_threadpool PRIVATE
    threadpool_impl.cpp
    threadpool_timer_impl.cpp
)

target_link_libraries(m_threadpool PUBLIC
    m_debugging
    m_formatters
)

target_include_directories(m_threadpool PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
