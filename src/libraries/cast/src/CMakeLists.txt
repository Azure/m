cmake_minimum_required(VERSION 3.23)

target_sources(m_cast PRIVATE
    cast.cpp
)

target_include_directories(m_cast PUBLIC
    ../include
)

target_link_libraries(m_cast PUBLIC
    m_math
)
