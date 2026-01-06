cmake_minimum_required(VERSION 3.23)

target_sources(m_multi_byte PRIVATE
    "acp_convert.cpp"
    convert_acp.cpp
    string_conversion_details.cpp
    multi_byte_to_utf.cpp
    to_acp_string.cpp
    to_span.cpp
    utf16_to_multi_byte.cpp
)

target_include_directories(m_multi_byte PUBLIC
    ../include
)

target_link_libraries(m_multi_byte PUBLIC
    m_errors
    m_math
    m_strings
    m_utf
)

list(APPEND m_installation_targets
    m_multi_byte
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
