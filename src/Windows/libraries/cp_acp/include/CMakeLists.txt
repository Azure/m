cmake_minimum_required(VERSION 3.23)

target_sources(m_cp_acp PUBLIC FILE_SET HEADERS FILES
    m/cp_acp/convert.h
    m/cp_acp/convert_acp_to.h
    m/cp_acp/convert_acp_to_utf16.h
    m/cp_acp/convert_core.h
    m/cp_acp/convert_to_acp.h
    m/cp_acp/convert_utf16_to_acp.h
    m/cp_acp/cp_acp.h
    m/cp_acp/to_string.h
)
