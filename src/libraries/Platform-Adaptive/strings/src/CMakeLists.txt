cmake_minimum_required(VERSION 3.23)

target_sources(m_platform_adaptive_strings PRIVATE
    platform_adaptive_strings.cpp
)

target_include_directories(m_platform_adaptive_strings PUBLIC
    ../include
)

target_link_libraries(m_platform_adaptive_strings PUBLIC
    m_error_handling
    m_strings
)

if(WIN32)

    target_link_libraries(m_platform_adaptive_strings PUBLIC
        m_windows_strings
    )

elseif(LINUX)

    target_link_libraries(m_platform_adaptive_strings PUBLIC
        m_linux_strings
    )

endif()

