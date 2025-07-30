cmake_minimum_required(VERSION 3.23)

target_sources(m_csv PRIVATE
    csv.cpp
    breaker.cpp)

target_include_directories(m_csv PUBLIC
    ../include
)

target_link_libraries(m_csv PUBLIC
    m_io
    m_math
)
