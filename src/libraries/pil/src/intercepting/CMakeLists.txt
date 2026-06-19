cmake_minimum_required(VERSION 3.23)

# Intercepting webcore is Windows-only (uses IAT patching).
if(WIN32)

target_sources(m_pil PRIVATE
    intercepting_webcore.cpp
)

target_link_libraries(m_pil PRIVATE
    dbghelp
)

endif()
