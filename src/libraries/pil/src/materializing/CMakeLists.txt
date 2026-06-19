cmake_minimum_required(VERSION 3.23)

# Materializing webcore decorator (D-HWC-4, M-HWC-MATERIALIZE)
# Only built on Windows (HWC is Windows-only).

if(WIN32)
    target_sources(m_pil PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/materializing_webcore.cpp
    )

    target_include_directories(m_pil PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
