// Copyright (c) Microsoft Corporation.
//
// MW17-5: egress-proof client for the Rust `windows-win32-shim` WinHTTP alias.
//
// This is the network-seam analogue of `linkproof_main.cpp`. It includes NO shim
// headers and calls the genuine WinHTTP entry points
// (WinHttpOpen / Connect / OpenRequest / SendRequest / ReceiveResponse /
// QueryHeaders / QueryDataAvailable / ReadData / CloseHandle, plus SetTimeouts).
// Whether those calls reach the live network or are diverted into the Rust shim
// is decided purely at link time:
//
//   * the *aliased* build links the alias COFF object emitted by `gen-alias-obj`,
//     whose `__imp_WinHttp*` IAT slots resolve to the shim's `mWinHttp*` exports
//     (via `windows_win32_shim.dll.lib`), so every WinHTTP call is rerouted into
//     the egress engine driven by the `<exe>.pilcfg` `egress` mode; and
//   * the *control* build links `winhttp.lib`, so the calls are genuine.
//
// CRITICAL: the alias roster also aliases `WriteFile` / `ReadFile`, so the CRT's
// `printf` -> `WriteFile(stdout)` is itself rerouted into the shim and the
// aliased build's stdout is SWALLOWED. The proof therefore carries its verdict
// solely in the PROCESS EXIT CODE (as `linkproof` does): `main` performs the
// assertion against the expectation passed on the command line and returns 0
// only when it holds. The diagnostic `printf`s are kept for the (genuine-stdout)
// control build and are harmless when swallowed.
//
// Usage: egressproof <host> <port> <verb> <path> <expect>
//   <expect> = "fail"                    expect the transaction to error
//            | "status=NNN"              expect a response with status NNN
//            | "status=NNN,body=MARKER"  ... whose body also contains MARKER

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    // Exit codes (the sole proof channel for the aliased build).
    enum : int
    {
        EXIT_MATCH = 0,           // expectation held
        EXIT_USAGE = 3,
        EXIT_OPEN_FAILED = 10,    // a WinHTTP step failed when success was expected
        EXIT_CONNECT_FAILED = 11,
        EXIT_REQUEST_FAILED = 12,
        EXIT_SEND_FAILED = 13,
        EXIT_RECEIVE_FAILED = 14,
        EXIT_QUERY_FAILED = 15,
        EXIT_READ_FAILED = 16,
        EXIT_STATUS_MISMATCH = 20,
        EXIT_BODY_MISMATCH = 21,
        EXIT_UNEXPECTED_SUCCESS = 40, // succeeded when "fail" was expected
    };

    std::wstring
    widen(const char* s)
    {
        if (s == nullptr)
        {
            return std::wstring();
        }
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 1)
        {
            return std::wstring();
        }
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
        return w;
    }

    // A WinHTTP step failed. If failure was expected, that is a PASS (0);
    // otherwise the step's failure code is the verdict.
    int
    step_failed(bool expect_fail, int fail_code, const char* step)
    {
        std::printf("egress-proof: %s failed gle=%lu (expect_fail=%d)\n",
                    step, ::GetLastError(), expect_fail ? 1 : 0);
        return expect_fail ? EXIT_MATCH : fail_code;
    }
}

int
main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::printf("usage: egressproof <host> <port> <verb> <path> <expect>\n");
        return EXIT_USAGE;
    }

    const std::wstring host = widen(argv[1]);
    const INTERNET_PORT port = static_cast<INTERNET_PORT>(std::atoi(argv[2]));
    const std::wstring verb = widen(argv[3]);
    const std::wstring path = widen(argv[4]);

    // Parse the expectation.
    const std::string expect = argv[5];
    const bool expect_fail = (expect == "fail");
    long expected_status = -1;
    std::string expected_body;
    if (!expect_fail)
    {
        const size_t sp = expect.find("status=");
        if (sp != std::string::npos)
        {
            expected_status = std::atol(expect.c_str() + sp + 7);
        }
        const size_t bp = expect.find("body=");
        if (bp != std::string::npos)
        {
            expected_body = expect.substr(bp + 5);
        }
    }

    std::printf("egress-proof: host=%s port=%d verb=%s path=%s expect=%s\n",
                argv[1], static_cast<int>(port), argv[3], argv[4], argv[5]);

    HINTERNET session = ::WinHttpOpen(L"egressproof/1.0",
                                      WINHTTP_ACCESS_TYPE_NO_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0);
    if (session == nullptr)
    {
        return step_failed(expect_fail, EXIT_OPEN_FAILED, "WinHttpOpen");
    }

    // Short timeouts so the genuine control fails fast against a closed port
    // (the shim's mWinHttpSetTimeouts is a no-op, which is fine).
    ::WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);

    HINTERNET connect = ::WinHttpConnect(session, host.c_str(), port, 0);
    if (connect == nullptr)
    {
        ::WinHttpCloseHandle(session);
        return step_failed(expect_fail, EXIT_CONNECT_FAILED, "WinHttpConnect");
    }

    HINTERNET request = ::WinHttpOpenRequest(connect,
                                             verb.c_str(),
                                             path.c_str(),
                                             nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             0);
    if (request == nullptr)
    {
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        return step_failed(expect_fail, EXIT_REQUEST_FAILED, "WinHttpOpenRequest");
    }

    // A small body for mutating verbs so the buffered journal captures something.
    const char* const body_bytes = "egressproof-body";
    const bool is_safe = (verb == L"GET" || verb == L"HEAD");
    const DWORD body_len = is_safe ? 0u : static_cast<DWORD>(std::strlen(body_bytes));

    if (::WinHttpSendRequest(request,
                             WINHTTP_NO_ADDITIONAL_HEADERS,
                             0,
                             body_len ? const_cast<char*>(body_bytes) : WINHTTP_NO_REQUEST_DATA,
                             body_len,
                             body_len,
                             0) == FALSE)
    {
        return step_failed(expect_fail, EXIT_SEND_FAILED, "WinHttpSendRequest");
    }

    if (::WinHttpReceiveResponse(request, nullptr) == FALSE)
    {
        return step_failed(expect_fail, EXIT_RECEIVE_FAILED, "WinHttpReceiveResponse");
    }

    DWORD status = 0;
    DWORD status_len = sizeof(status);
    if (::WinHttpQueryHeaders(request,
                              WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX,
                              &status,
                              &status_len,
                              WINHTTP_NO_HEADER_INDEX) == FALSE)
    {
        return step_failed(expect_fail, EXIT_QUERY_FAILED, "WinHttpQueryHeaders");
    }

    std::string response_body;
    for (;;)
    {
        DWORD available = 0;
        if (::WinHttpQueryDataAvailable(request, &available) == FALSE)
        {
            return step_failed(expect_fail, EXIT_READ_FAILED, "WinHttpQueryDataAvailable");
        }
        if (available == 0)
        {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (::WinHttpReadData(request, &chunk[0], available, &read) == FALSE)
        {
            return step_failed(expect_fail, EXIT_READ_FAILED, "WinHttpReadData");
        }
        if (read == 0)
        {
            break;
        }
        response_body.append(chunk.data(), read);
    }

    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);

    std::printf("egress-proof: got status=%lu body-bytes=%zu\n",
                status, response_body.size());

    // A response arrived. If failure was the expectation, that is a FAIL.
    if (expect_fail)
    {
        return EXIT_UNEXPECTED_SUCCESS;
    }
    if (expected_status >= 0 && static_cast<long>(status) != expected_status)
    {
        return EXIT_STATUS_MISMATCH;
    }
    if (!expected_body.empty() && response_body.find(expected_body) == std::string::npos)
    {
        return EXIT_BODY_MISMATCH;
    }
    return EXIT_MATCH;
}
