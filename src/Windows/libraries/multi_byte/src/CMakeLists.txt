cmake_minimum_required(VERSION 3.23)

target_sources(m_multi_byte PRIVATE
    "multi_byte_to_utf16.cpp"
    string_conversion_details.cpp
    to_span.cpp
    utf16_to_multi_byte.cpp
    utf16_to_multi_byte_length.cpp
    view_to_tstring.cpp
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
