cmake_minimum_required(VERSION 3.23)

# The direct provider has no Linux implementation yet: create_platform_interface
# takes the `#else` (M_NOT_IMPLEMENTED) branch on non-Windows builds, exactly as
# it does for the registry. Because that throws before any platform object is
# constructed, neither the registry nor the filesystem provider needs Linux
# sources here. The filesystem surface still compiles on Linux through its
# platform-neutral interfaces, base types, value wrappers, and null provider.
target_sources(m_pil PRIVATE
)

target_link_libraries(m_pil PUBLIC
    m_linux_strings
    m_threadpool
)

target_include_directories(m_pil PUBLIC
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)
