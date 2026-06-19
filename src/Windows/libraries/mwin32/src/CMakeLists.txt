cmake_minimum_required(VERSION 3.23)

find_package(nlohmann_json CONFIG REQUIRED)

# Internal static library containing implementation that tests need access to.
# The DLL links this (whole-archive) and tests link it directly. This avoids
# manually listing internal sources in test/CMakeLists.txt.
add_library(m_mwin32_internal STATIC
    pilcfg.cpp
    session.cpp
    webcore_config_platform.cpp
)

target_compile_features(m_mwin32_internal PUBLIC ${M_CXX_STD})

target_include_directories(m_mwin32_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../include
)

target_link_libraries(m_mwin32_internal PUBLIC
    m_cast
    m_cp_acp
    m_errors
    m_error_handling
    m_pil
    m_sstring
    m_strings
    m_windows_chrono
    m_windows_strings
    nlohmann_json::nlohmann_json
)

target_sources(m_mwin32 PRIVATE
    handle_table.cpp
    mwinfile.cpp
    mwinhwc.cpp
    mwinreg.cpp
    mwinsock.cpp
)

target_include_directories(m_mwin32 PUBLIC
    ../include
)

# Link internal library with whole-archive so all symbols are exported
target_link_libraries(m_mwin32 PUBLIC
    m_mwin32_internal
)

target_link_libraries(m_mwin32 PRIVATE
    nlohmann_json::nlohmann_json
    ws2_32
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
