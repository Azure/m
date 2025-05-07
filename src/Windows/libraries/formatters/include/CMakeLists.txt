cmake_minimum_required(VERSION 3.23)

target_sources(m_formatters PUBLIC FILE_SET HEADERS FILES
    m/formatters/FILETIME.h
    m/formatters/FILE_ACTION.h
    m/formatters/FILE_NOTIFY_EXTENDED_INFORMATION.h
    m/formatters/FILE_NOTIFY_INFORMATION.h
    m/formatters/NTSTATUS.h
    m/formatters/OVERLAPPED.h
    m/formatters/Win32ErrorCode.h
)
