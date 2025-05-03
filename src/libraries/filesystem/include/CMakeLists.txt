cmake_minimum_required(VERSION 3.23)

target_sources(m_filesystem PUBLIC FILE_SET HEADERS FILES 
    m/filesystem/filesystem.h 
    m/filesystem/filesystem_loadstore.h
    m/filesystem/filesystem_objects.h
    m/filesystem/filesystem_paths.h
)
