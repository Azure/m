cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PRIVATE
)

target_link_libraries(m_pil PUBLIC
    m_threadpool
)

target_include_directories(m_pil PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
