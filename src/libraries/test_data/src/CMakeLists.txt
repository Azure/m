cmake_minimum_required(VERSION 3.23)

target_sources(m_test_data PRIVATE
    test_data.cpp
)

target_include_directories(m_test_data PUBLIC
    ../include
)

target_link_libraries(m_test_data PUBLIC
#    m_error_handling
)
