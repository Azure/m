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
#include <m/utility/pointers.h>

using namespace std::string_view_literals;

namespace m::pe2l
{
    static inline constexpr auto known_apiset_dlls = {
        //
        L"api-ms-win-core-console-l1-1-.dll"sv,
        L"api-ms-win-core-datetime-l1-1-0.dll"sv,
        L"api-ms-win-core-debug-l1-1-0.dll"sv,
        L"api-ms-win-core-debug-minidump-l1-1-0.dll"sv,
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
        L"api-ms-win-core-path-l1-1-0.dll"sv,
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
        L"api-ms-win-crt-multibyte-l1-1-0.dll"sv,
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
        L"netutils.dll"sv,
        L"newdev.dll"sv,
        L"ntdll.dll"sv,
        L"normaliz.dll"sv,
        L"ole32.dll"sv,
        L"oleaut32.dll"sv,
        L"pdh.dll"sv,
        L"psapi.dll"sv,
        L"rpcrt4.dll"sv,
        L"secur32.dll"sv,
        L"setupapi.dll"sv,
        L"shell32.dll"sv,
        L"shlwapi.dll"sv,
        L"srvcli.dll"sv,
        L"user32.dll"sv,
        L"userenv.dll"sv,
        L"version.dll"sv,
        L"virtdisk.dll"sv,
        L"webservices.dll"sv,
        L"wevtapi.dll"sv,
        L"winhttp.dll"sv,
        L"wininet.dll"sv,
        L"wldap32.dll"sv,
        L"ws2_32.dll"sv,
        L"wtsapi32.dll"sv,
        L"xmllite.dll"sv,
    };
} // namespace m::pe2l
