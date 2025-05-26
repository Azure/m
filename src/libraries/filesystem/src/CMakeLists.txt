cmake_minimum_required(VERSION 3.23)

target_sources(m_filesystem PRIVATE
    filesystem.cpp
    loadstore.cpp
    make_path.cpp
    path_conversions.cpp
    seekable_input_file.cpp
    seekable_output_file.cpp
 "monitor.cpp")

if(WIN32)
    target_sources(m_filesystem PRIVATE
        windows_loadstore.cpp 
    )
endif()

target_compile_definitions(m_filesystem PRIVATE _CRT_SECURE_NO_WARNINGS)

target_link_libraries(m_filesystem PUBLIC
    m_block_buffer
    m_byte_streams
    m_cast
    m_math
    m_memory
    m_strings
)

if(WIN32)
    target_link_libraries(m_filesystem PUBLIC
        m_windows_strings
    )
else()
    target_link_libraries(m_filesystem PUBLIC
        m_linux_strings
    )

endif()

# Include platform-specific code
add_subdirectory(platforms)

target_include_directories(m_filesystem PUBLIC
    ../include
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
