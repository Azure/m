// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include <m/pe/pe_decoder.h>

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        static inline constexpr auto known_apiset_dlls = {
            //
            L"api-ms-win-core-console-l1-1-.dll"sv,
            L"api-ms-win-core-datetime-l1-1-0.dll"sv,
            L"api-ms-win-core-debug-l1-1-0.dll"sv,
            L"api-ms-win-core-errorhandling-l1-1-0.dll"sv,
            L"api-ms-win-core-file-l1-1-0.dll"sv,
            L"api-ms-win-core-file-l1-2-0.dll"sv,
            L"api-ms-win-core-file-l2-1-0.dll"sv,
            L"api-ms-win-core-handle-l1-1-0.dll"sv,
            L"api-ms-win-core-heap-l1-1-0.dll"sv,
            L"api-ms-win-core-heap-l2-1-0.dll"sv,
            L"api-ms-win-core-interlocked-l1-1-0.dll"sv,
            L"api-ms-win-core-io-l1-1-0.dll"sv,
            L"api-ms-win-core-kernel32-legacy-l1-1-0.dll"sv,
            L"api-ms-win-core-libraryloader-l1-1-0.dll"sv,
            L"api-ms-win-core-libraryloader-l1-2-0.dll"sv,
            L"api-ms-win-core-localization-l1-2-0.dll"sv,
            L"api-ms-win-core-memory-l1-1-0.dll"sv,
            L"api-ms-win-core-namedpipe-l1-1-0.dll"sv,
            L"api-ms-win-core-processenvironment-l1-1-0.dll"sv,
            L"api-ms-win-core-processthreads-l1-1-0.dll"sv,
            L"api-ms-win-core-processthreads-l1-1-1.dll"sv,
            L"api-ms-win-core-profile-l1-1-0.dll"sv,
            L"api-ms-win-core-registry-l1-1-0.dll"sv,
            L"api-ms-win-core-rtlsupport-l1-1-0.dll"sv,
            L"api-ms-win-core-string-l1-1-0.dll"sv,
            L"api-ms-win-core-synch-l1-1-0.dll"sv,
            L"api-ms-win-core-synch-l1-2-0.dll"sv,
            L"api-ms-win-core-sysinfo-l1-1-0.dll"sv,
            L"api-ms-win-core-timezone-l1-1-0.dll"sv,
            L"api-ms-win-core-util-l1-1-0.dll"sv,
            L"api-ms-win-crt-conio-l1-1-0.dll"sv,
            L"api-ms-win-crt-convert-l1-1-0.dll"sv,
            L"api-ms-win-crt-environment-l1-1-0.dll"sv,
            L"api-ms-win-crt-filesystem-l1-1-0.dll"sv,
            L"api-ms-win-crt-heap-l1-1-0.dll"sv,
            L"api-ms-win-crt-locale-l1-1-0.dll"sv,
            L"api-ms-win-crt-math-l1-1-0.dll"sv,
            L"api-ms-win-crt-multi_byte-l1-1-0.dll"sv,
            L"api-ms-win-crt-private-l1-1-0.dll"sv,
            L"api-ms-win-crt-process-l1-1-0.dll"sv,
            L"api-ms-win-crt-runtime-l1-1-0.dll"sv,
            L"api-ms-win-crt-stdio-l1-1-0.dll"sv,
            L"api-ms-win-crt-string-l1-1-0.dll"sv,
            L"api-ms-win-crt-time-l1-1-0.dll"sv,
            L"api-ms-win-crt-utility-l1-1-0.dll"sv,
            L"api-ms-win-devices-config-l1-1-1.dll"sv,
            L"api-ms-win-downlevel-advapi32-l1-1-0.dll"sv,
            L"api-ms-win-downlevel-ole32-l1-1-0.dll"sv,
            L"api-ms-win-downlevel-shlwapi-l1-1-0.dll"sv,
            L"api-ms-win-eventing-provider-l1-1-0.dll"sv,
            L"api-ms-win-service-management-l1-1-0.dll"sv,
            L"api-ms-win-service-management-l2-1-0.dll"sv,
            //
            //
            //
            L"advapi32.dll"sv,
            L"bcrypt.dll"sv,
            L"bcryptprimitives.dll"sv,
            L"cfgmgr32.dll"sv,
            L"crypt32.dll"sv,
            L"dbghelp.dll"sv,
            L"devobj.dll"sv,
            L"dnsapi.dll"sv,
            L"fltlib.dll"sv,
            L"fwpuclnt.dll"sv,
            L"gdi32.dll"sv,
            L"httpapi.dll"sv,
            L"iphlpapi.dll"sv,
            L"kernel32.dll"sv,
            L"miutils.dll"sv,
            L"mscoree.dll"sv,
            L"msvcrt.dll"sv,
            L"ncrypt.dll"sv,
            L"netapi32.dll"sv,
            L"newdev.dll"sv,
            L"ntdll.dll"sv,
            L"ole32.dll"sv,
            L"oleaut32.dll"sv,
            L"psapi.dll"sv,
            L"rpcrt4.dll"sv,
            L"secur32.dll"sv,
            L"setupapi.dll"sv,
            L"shell32.dll"sv,
            L"shlwapi.dll"sv,
            L"user32.dll"sv,
            L"userenv.dll"sv,
            L"version.dll"sv,
            L"virtdisk.dll"sv,
            L"webservices.dll"sv,
            L"wevtapi.dll"sv,
            L"winhttp.dll"sv,
            L"wininet.dll"sv,
            L"ws2_32.dll"sv,
            L"xmllite.dll"sv,
        };

        class loader_context
        {
        public:
            loader_context();

            loader_context(loader_context const&) = delete;

            void
            operator=(loader_context const&) = delete;

            ~loader_context() = default;

            void
            resolve(std::filesystem::path const& path);

            std::size_t
            unresolved_count() const;

            template <typename Fn>
            void
            for_each_not_found(Fn fn)
            {
#if 0
                std::ranges::for_each(m_resolved,
                                      [&](auto pair) {
                                          if (pair.second.not_found())
                                              std::invoke(std::forward<Fn>(fn), pair.second.name());
                                      });
#else
                for (auto&& p: m_resolved)
                {
                    if (p.second.not_found())
                        std::invoke(fn, p.second.name());
                }
#endif
            }

        protected:
            std::optional<std::filesystem::path>
            try_resolve(std::wstring_view name);

            std::queue<std::wstring> m_pending;

            class pe_record
            {
            public:
                enum pe_not_found_t
                {
                    pe_not_found,
                };

                pe_record(std::filesystem::path const& path, gsl::not_null<loader_context*> loader);

                //
                // Constructor without a loader context is for "known PEs" where
                // the loader can snip the traversal short.
                //
                pe_record(std::wstring_view view);

                pe_record(std::wstring_view, pe_not_found_t);

                std::wstring
                name() const;

                bool
                not_found() const;

            protected:
                std::wstring m_name;

                bool m_not_found;

                // The rest may have been omitted; the record may be faux
                std::filesystem::path           m_path;
                std::unique_ptr<m::pe::decoder> m_decoder;
            };

            std::set<std::wstring>               m_known_pes;
            std::map<std::wstring, pe_record>    m_resolved;
            std::vector<std::filesystem::path> m_search_path;
            std::size_t                        m_unresolved_count;
        };
    } // namespace pe
} // namespace m
