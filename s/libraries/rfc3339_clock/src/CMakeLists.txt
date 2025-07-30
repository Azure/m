cmake_minimum_required(VERSION 3.23)

target_sources(m_rfc3339_clock PRIVATE
    rfc3339_clock.cpp
)

target_include_directories(m_rfc3339_clock PUBLIC
    ../include
)

target_link_libraries(m_rfc3339_clock PUBLIC
)
