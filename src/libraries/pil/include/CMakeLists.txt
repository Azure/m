cmake_minimum_required(VERSION 3.23)

target_sources(m_pil PUBLIC FILE_SET HEADERS FILES 
    m/pil/common.h
    m/pil/disposition.h
    m/pil/platform.h
    m/pil/platform_interfaces.h
    m/pil/registry.h
    m/pil/registry_base_types.h
    m/pil/registry_interfaces.h
    m/pil/security_attributes.h
)
