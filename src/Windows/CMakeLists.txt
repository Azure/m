cmake_minimum_required(VERSION 3.23)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    STRICT
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000000
    NOMINMAX
)

add_subdirectory(libraries)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
