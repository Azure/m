cmake_minimum_required(VERSION 3.23)

target_sources(m_chonk PRIVATE
    chonk.cpp
)

target_include_directories(m_chonk PUBLIC
    ../include
)

target_link_libraries(m_chonk PUBLIC
    m_error_handling
)
