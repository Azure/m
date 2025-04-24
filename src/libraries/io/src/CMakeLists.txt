cmake_minimum_required(VERSION 3.23)

target_sources(m_io PRIVATE
    io.cpp
)

target_link_libraries(m_io PUBLIC
    m_cast
    m_math
)

target_include_directories(m_io PUBLIC
    ../include
)
