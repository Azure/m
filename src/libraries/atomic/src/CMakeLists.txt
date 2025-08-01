cmake_minimum_required(VERSION 3.23)

target_sources(m_atomic PRIVATE
    atomic.cpp
)

target_include_directories(m_atomic PUBLIC
    ../include
)

target_link_libraries(m_atomic PUBLIC
)
