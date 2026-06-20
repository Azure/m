cmake_minimum_required(VERSION 3.23)

# An ordinary Win32 registry client (no mwin32 headers) that becomes redirectable
# purely by linking the mwin32_alias object. See COMPONENT.md and DESIGN-NOTES D8.
add_executable(mwin32_sample_client mwin32_sample_client.cpp)

target_compile_features(mwin32_sample_client PUBLIC ${M_CXX_STD})

target_link_libraries(mwin32_sample_client PRIVATE mwin32_alias)

# m_mwin32.dll lives in a sibling build directory, so copy every runtime DLL
# dependency next to the sample so it can launch.
add_custom_command(TARGET mwin32_sample_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:mwin32_sample_client> $<TARGET_FILE_DIR:mwin32_sample_client>
    COMMAND_EXPAND_LISTS
)

# An ordinary Win32 filesystem client (no mwin32 headers) that becomes redirectable
# purely by linking the mwin32_alias object (M-FS-SHIM-8). It drives the filesystem
# shim ABI (CreateDirectoryW / CreateFileW / GetFileAttributesExW / FindFirstFileW /
# MoveFileExW) through the alias while CloseHandle stays un-aliased.
add_executable(mwin32_fs_sample_client mwin32_fs_sample_client.cpp)

target_compile_features(mwin32_fs_sample_client PUBLIC ${M_CXX_STD})

target_link_libraries(mwin32_fs_sample_client PRIVATE mwin32_alias)

add_custom_command(TARGET mwin32_fs_sample_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:mwin32_fs_sample_client> $<TARGET_FILE_DIR:mwin32_fs_sample_client>
    COMMAND_EXPAND_LISTS
)

# An ordinary Win32 filesystem notification client (no mwin32 headers) that becomes
# redirectable purely by linking the mwin32_alias object (M-FS-NOTIFY-REDIR-1). It
# opens a directory with FILE_FLAG_BACKUP_SEMANTICS, registers a watch via
# ReadDirectoryChangesW (aliased through mwin32), and reports notifications to stdout.
add_executable(mwin32_notify_sample_client mwin32_notify_sample_client.cpp)

target_compile_features(mwin32_notify_sample_client PUBLIC ${M_CXX_STD})

target_link_libraries(mwin32_notify_sample_client PRIVATE mwin32_alias)

add_custom_command(TARGET mwin32_notify_sample_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:mwin32_notify_sample_client> $<TARGET_FILE_DIR:mwin32_notify_sample_client>
    COMMAND_EXPAND_LISTS
)

# An ordinary raw-Winsock HTTP/1.1 server (no mwin32 headers) for the wire-capture
# demo (WC-6). It calls genuine socket / bind / listen / accept / recv / send /
# closesocket; linking the mwin32_alias object retargets the data-transfer calls
# into the shim, which tees the bytes to the capture seam. `--family ipv4|ipv6|dual`
# and `--port N` (0 = ephemeral, echoed as port=<N>) choose the topology; a fault
# switch emits a non-conforming response without breaking the connection.
add_executable(mwin32_http_server_sample mwin32_http_server_sample.cpp)

target_compile_features(mwin32_http_server_sample PUBLIC ${M_CXX_STD})

target_link_libraries(mwin32_http_server_sample PRIVATE mwin32_alias ws2_32)

add_custom_command(TARGET mwin32_http_server_sample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:mwin32_http_server_sample> $<TARGET_FILE_DIR:mwin32_http_server_sample>
    COMMAND_EXPAND_LISTS
)

