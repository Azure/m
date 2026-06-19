cmake_minimum_required(VERSION 3.23)

if(BUILD_TESTING)
    include(GoogleTest)

    find_package(nlohmann_json CONFIG REQUIRED)

    add_executable(test_mwin32
        test_handle_table.cpp
        test_mwinhwc.cpp
        test_mwinreg_open_close.cpp
        test_mwinreg_predefined.cpp
        test_mwinreg_value_ops.cpp
        test_contractcfg.cpp
        test_pilcfg.cpp
        test_session_snapshot.cpp
    )

    target_compile_features(test_mwin32 PUBLIC ${M_CXX_STD})

    # Link the internal static library which provides pilcfg, session, and
    # webcore_config_platform. This avoids manually listing internal sources.
    target_link_libraries(
        test_mwin32
        m_mwin32
        m_mwin32_internal
        m_strings
        nlohmann_json::nlohmann_json
        GTest::gtest_main
    )

    enable_testing()

    # m_mwin32 is a shared library that lives in a sibling build directory, so
    # the test executable cannot locate it (or any other runtime DLL dependency)
    # at launch/discovery time. Copy all runtime DLL dependencies next to the
    # test binary so it can start.
    add_custom_command(TARGET test_mwin32 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32> $<TARGET_FILE_DIR:test_mwin32>
        COMMAND_EXPAND_LISTS
    )

    # The value-operation tests must run against the in-memory buffered overlay
    # rather than the live registry. The shim reads its configuration from a
    # `.pilcfg` sidecar next to the executable, so generate one that enables
    # buffer_updates for the test process.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32>.pilcfg
        CONTENT "{\"buffer_updates\": true}\n"
    )

    gtest_discover_tests(test_mwin32)

    # Link-proof integration test (M-ALIAS-4). This executable includes no mwin32
    # headers; it calls genuine Win32 registry APIs and links mwin32_alias, which
    # redirects those calls into the shim. It is a separate executable so the alias
    # only affects code that intends to be redirected.
    add_executable(test_mwin32_alias
        test_mwin32_alias.cpp
    )

    target_compile_features(test_mwin32_alias PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_alias
        mwin32_alias
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_alias POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_alias> $<TARGET_FILE_DIR:test_mwin32_alias>
        COMMAND_EXPAND_LISTS
    )

    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_alias>.pilcfg
        CONTENT "{\"buffer_updates\": true}\n"
    )

    gtest_discover_tests(test_mwin32_alias)

    # Sample-client lifecycle harness (M-SAMPLE). Drives the mwin32_sample_client
    # executable as a subprocess under different .pilcfg sidecars. It links no
    # mwin32 code itself; it only needs the sample (and its copied DLL) to exist,
    # so it depends on that target.
    add_executable(test_mwin32_sample
        test_mwin32_sample.cpp
    )

    target_compile_features(test_mwin32_sample PUBLIC ${M_CXX_STD})

    # The fault scenario authors a fault-script rule whose path must match the
    # path the sample's key reports, so the harness probes that path through the
    # PIL public API (and renders the script XML via pugixml, brought in
    # transitively by m_pil's public headers).
    target_link_libraries(
        test_mwin32_sample
        m_pil
        m_strings
        GTest::gtest_main
    )

    add_dependencies(test_mwin32_sample mwin32_sample_client)

    # The filesystem scenario (M-FS-SHIM-8) drives a second sample client through
    # the filesystem shim ABI, so the harness also needs that executable built.
    add_dependencies(test_mwin32_sample mwin32_fs_sample_client)

    # The notification scenario (M-FS-NOTIFY-REDIR-1) drives a third sample client
    # through the redirected notify path, so that executable must be built too.
    add_dependencies(test_mwin32_sample mwin32_notify_sample_client)

    # Plumb the configuration-resolved sample output directory to the harness.
    # Under a multi-config generator (e.g. Visual Studio on CI) the samples land
    # in a per-config subdirectory (sample/<Config>/) that the harness cannot
    # derive from its own module path; a generated sidecar next to the test
    # binary carries the real directory so it launches the right executables
    # regardless of generator. std::filesystem::path accepts whichever separator
    # the generator emits, so no escaping concerns arise.
    file(GENERATE
        OUTPUT $<TARGET_FILE_DIR:test_mwin32_sample>/mwin32_sample_clients.dir
        CONTENT "$<TARGET_FILE_DIR:mwin32_sample_client>\n"
    )

    # m_pil is a shared library in a sibling build directory; copy its runtime
    # DLL dependencies next to the harness so it can start.
    add_custom_command(TARGET test_mwin32_sample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_sample> $<TARGET_FILE_DIR:test_mwin32_sample>
        COMMAND_EXPAND_LISTS
    )

    gtest_discover_tests(test_mwin32_sample)

    # Handle-based metadata family tests (M-FS-HANDLE-META-4). Links m_mwin32 and
    # drives the m-prefixed filesystem entry points in-process under a buffered +
    # redirecting .pilcfg, so it can open a file by mCreateFileW and read its
    # metadata back by handle.
    add_executable(test_mwin32_fsmeta
        test_mwinfile_handle_meta.cpp
    )

    target_compile_features(test_mwin32_fsmeta PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fsmeta
        m_mwin32
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_fsmeta POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fsmeta> $<TARGET_FILE_DIR:test_mwin32_fsmeta>
        COMMAND_EXPAND_LISTS
    )

    # Enable the buffered overlay (so writes stay in memory, never on the live
    # disk) and a single filesystem redirection (so the private->public final
    # path mapping is exercised). The redirection key is a drive-relative prefix
    # with no leading separator: file_path's drive root absorbs the terminating
    # separator, so the relative path of "C:\mwin32_meta_pub\..." begins
    # "mwin32_meta_pub" with no leading backslash.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_fsmeta>.pilcfg
        CONTENT "{\"buffer_updates\": true, \"redirections\": [{\"from\": \"mwin32_meta_pub\", \"to\": \"mwin32_meta_priv\"}]}\n"
    )

    gtest_discover_tests(test_mwin32_fsmeta)

    # Path-based copy / replace / temp-file / path-resolution family tests
    # (M-FS-COPY-5). Links m_mwin32 and drives the m-prefixed filesystem entry
    # points in-process under a buffered + redirecting .pilcfg, so copies, the
    # replace re-keying, temp-file minting, and path canonicalization all run
    # through the provider chain without touching the live disk.
    add_executable(test_mwin32_fscopy
        test_mwinfile_copy.cpp
    )

    target_compile_features(test_mwin32_fscopy PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fscopy
        m_mwin32
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_fscopy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fscopy> $<TARGET_FILE_DIR:test_mwin32_fscopy>
        COMMAND_EXPAND_LISTS
    )

    # Enable the buffered overlay (so created nodes stay in memory, never on the
    # live disk) and a single filesystem redirection (so the private->public
    # final path mapping is exercised). The redirection key is a drive-relative
    # prefix with no leading separator: file_path's drive root absorbs the
    # terminating separator, so the relative path of "C:\mwin32_copy_pub\..."
    # begins "mwin32_copy_pub" with no leading backslash.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_fscopy>.pilcfg
        CONTENT "{\"buffer_updates\": true, \"redirections\": [{\"from\": \"mwin32_copy_pub\", \"to\": \"mwin32_copy_priv\"}]}\n"
    )

    gtest_discover_tests(test_mwin32_fscopy)

    # Dusty-deck legacy open / create family tests (M-FS-LEGACY-1). Links
    # m_mwin32 and drives the ANSI mOpenFile / _lopen / _lcreat entry points
    # in-process under a buffered + redirecting .pilcfg, so the legacy HFILE
    # mint, the OF_* style mapping, the OF_EXIST probe, and the OFSTRUCT public
    # path fill all run through the provider chain without touching the live
    # disk.
    add_executable(test_mwin32_fslegacy
        test_mwinfile_legacy.cpp
    )

    target_compile_features(test_mwin32_fslegacy PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fslegacy
        m_mwin32
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_fslegacy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fslegacy> $<TARGET_FILE_DIR:test_mwin32_fslegacy>
        COMMAND_EXPAND_LISTS
    )

    # Enable the buffered overlay (so created nodes stay in memory, never on the
    # live disk) and a single filesystem redirection (so the OFSTRUCT public
    # path fill is proved against a distinct private backing path). The
    # redirection key is a drive-relative prefix with no leading separator:
    # file_path's drive root absorbs the terminating separator, so the relative
    # path of "C:\mwin32_legacy_pub\..." begins "mwin32_legacy_pub" with no
    # leading backslash.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_fslegacy>.pilcfg
        CONTENT "{\"buffer_updates\": true, \"redirections\": [{\"from\": \"mwin32_legacy_pub\", \"to\": \"mwin32_legacy_priv\"}]}\n"
    )

    gtest_discover_tests(test_mwin32_fslegacy)

    # Transacted (TxF) family tests (M-FS-LEGACY-2). Links m_mwin32 and drives
    # the m-prefixed transacted entry points in-process under a buffered +
    # redirecting .pilcfg. Each call passes a bogus non-null transaction handle
    # to prove the shim ignores it (D11) and still forwards to the
    # non-transacted sibling through the provider chain, never touching the live
    # disk.
    add_executable(test_mwin32_fstxf
        test_mwinfile_transacted.cpp
    )

    target_compile_features(test_mwin32_fstxf PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fstxf
        m_mwin32
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_fstxf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fstxf> $<TARGET_FILE_DIR:test_mwin32_fstxf>
        COMMAND_EXPAND_LISTS
    )

    # Enable the buffered overlay (so created nodes stay in memory, never on the
    # live disk) and a single filesystem redirection (so the forwarded ops
    # inherit the sibling's private->public mapping). The redirection key is a
    # drive-relative prefix with no leading separator: file_path's drive root
    # absorbs the terminating separator, so the relative path of
    # "C:\mwin32_txf_pub\..." begins "mwin32_txf_pub" with no leading backslash.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_fstxf>.pilcfg
        CONTENT "{\"buffer_updates\": true, \"redirections\": [{\"from\": \"mwin32_txf_pub\", \"to\": \"mwin32_txf_priv\"}]}\n"
    )

    gtest_discover_tests(test_mwin32_fstxf)

    # Directory change-notification family tests (M-FS-NOTIFY-3). Links m_mwin32
    # and drives the change-notification entry points in-process. Unlike the
    # other filesystem suites this one runs under a *passthrough* .pilcfg (no
    # buffering, no redirection): change notifications are observed only by the
    # live provider's real ReadDirectoryChangesW -- the buffered overlay does not
    # model live change -- so the watch targets a real OS scratch directory and
    # observes real mutations.
    add_executable(test_mwin32_fsnotify
        test_mwinfile_notify.cpp
    )

    target_compile_features(test_mwin32_fsnotify PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fsnotify
        m_mwin32
        GTest::gtest_main
    )

    add_custom_command(TARGET test_mwin32_fsnotify POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fsnotify> $<TARGET_FILE_DIR:test_mwin32_fsnotify>
        COMMAND_EXPAND_LISTS
    )

    # Passthrough configuration: no buffering and no redirection, so calls reach
    # the live (direct) provider and its real ReadDirectoryChangesW-backed
    # monitor, which is the only provider that observes live filesystem change.
    file(GENERATE
        OUTPUT $<TARGET_FILE:test_mwin32_fsnotify>.pilcfg
        CONTENT "{}\n"
    )

    gtest_discover_tests(test_mwin32_fsnotify)

    # Byte-content & positioning family tests (M-FS-CONTENT-4). Links m_mwin32
    # and drives the m-prefixed content / positioning entry points in-process.
    # Unlike the buffered suites this one needs *writable* content, which the
    # buffered overlay does not model, so it runs under a redirecting-over-direct
    # stack (buffer_updates = false): writes land as real bytes in a private
    # backing directory next to the executable. The .pilcfg is authored at
    # runtime by the suite's own main() (it must encode the executable's actual
    # directory, unknown at configure time), so this target supplies a custom
    # entry point and links GTest::gtest rather than GTest::gtest_main.
    add_executable(test_mwin32_fscontent
        test_mwinfile_content.cpp
    )

    target_compile_features(test_mwin32_fscontent PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fscontent
        m_mwin32
        GTest::gtest
    )

    add_custom_command(TARGET test_mwin32_fscontent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fscontent> $<TARGET_FILE_DIR:test_mwin32_fscontent>
        COMMAND_EXPAND_LISTS
    )

    gtest_discover_tests(test_mwin32_fscontent)

    # Dusty-deck legacy content family tests (M-FS-LEGACY-4). Links m_mwin32 and
    # drives the legacy _l* / _h* byte primitives and the LZ compress / expand
    # family in-process. Like the content suite it needs writable content, so it
    # runs under a redirecting-over-direct stack (buffer_updates = false) whose
    # .pilcfg is authored at runtime by the suite's own main(); the target
    # therefore supplies a custom entry point and links GTest::gtest rather than
    # GTest::gtest_main.
    add_executable(test_mwin32_fslegacycontent
        test_mwinfile_legacy_content.cpp
    )

    target_compile_features(test_mwin32_fslegacycontent PUBLIC ${M_CXX_STD})

    target_link_libraries(
        test_mwin32_fslegacycontent
        m_mwin32
        GTest::gtest
    )

    add_custom_command(TARGET test_mwin32_fslegacycontent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_mwin32_fslegacycontent> $<TARGET_FILE_DIR:test_mwin32_fslegacycontent>
        COMMAND_EXPAND_LISTS
    )

    gtest_discover_tests(test_mwin32_fslegacycontent)
endif()
