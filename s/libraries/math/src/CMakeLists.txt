cmake_minimum_required(VERSION 3.23)

target_sources(m_math PRIVATE
    math.cpp
)

target_include_directories(m_math PUBLIC
    ../include
)

target_link_libraries(m_math PUBLIC
    m_cast
)
