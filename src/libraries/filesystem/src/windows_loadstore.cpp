// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define NOMINMAX

#include <cstdint>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/filesystem/filesystem_loadstore.h>

#include <Windows.h>

namespace
{
    //
    // Equivalent to a subset of wil::unique_hfile. Getting wil to integrate
    // with the github CI is a pain so not using it at this time. Unfortunate.
    //
    class unique_hfile
    {
    public:
        constexpr unique_hfile(): m_h(INVALID_HANDLE_VALUE) {}
        constexpr unique_hfile(HANDLE h): m_h(h) {}
        ~unique_hfile()
        {
            auto const h = std::exchange(m_h, INVALID_HANDLE_VALUE);
            if ((h != nullptr) && (h != INVALID_HANDLE_VALUE))
                ::CloseHandle(h);
        }

        constexpr HANDLE
        get() const
        {
            return m_h;
        }

    private:
        HANDLE m_h;
    };
} // namespace

namespace m::filesystem
{
    std::vector<std::byte>
    load(std::filesystem::path const& path)
    {
        unique_hfile file{::CreateFileW(path.c_str(),
                                             GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             nullptr,
                                             OPEN_EXISTING,
                                             0,
                                             nullptr)};
        if (file.get() == INVALID_HANDLE_VALUE)
            throw_last_win32_error();

        FILE_STANDARD_INFO fsi{};

        if (!::GetFileInformationByHandleEx(file.get(), FileStandardInfo, &fsi, sizeof(fsi)))
            throw_last_win32_error();

        uint64_t               fileSize = fsi.EndOfFile.QuadPart;
        std::vector<std::byte> result(fileSize);
        auto                   outIterator = result.begin();
        auto const             bytesToRead = static_cast<DWORD>(std::min(1ull << 20, fileSize));

        for (;;)
        {
            DWORD bytesRead{};
            if (!::ReadFile(file.get(), &*outIterator, bytesToRead, &bytesRead, nullptr))
                throw_last_win32_error();

            if (bytesRead == 0)
                break;

            outIterator += bytesRead;
        }

        return result;
    }

    std::optional<std::vector<std::byte>>
    load(std::filesystem::path const& path, std::error_code& ec)
    {
        unique_hfile file{::CreateFileW(path.c_str(),
                                             GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                             nullptr,
                                             OPEN_EXISTING,
                                             0,
                                             nullptr)};
        if (file.get() == INVALID_HANDLE_VALUE)
        {
            ec = get_last_win32_error();
            return std::nullopt;
        }

        FILE_STANDARD_INFO fsi{};

        if (!::GetFileInformationByHandleEx(file.get(), FileStandardInfo, &fsi, sizeof(fsi)))
        {
            ec = get_last_win32_error();
            return std::nullopt;
        }

        uint64_t               fileSize = fsi.EndOfFile.QuadPart;
        std::vector<std::byte> result(fileSize);
        auto                   outIterator = result.begin();
        auto const             bytesToRead = static_cast<DWORD>((std::min)(1ull << 20, fileSize));

        for (;;)
        {
            DWORD bytesRead{};
            if (!::ReadFile(file.get(), &*outIterator, bytesToRead, &bytesRead, nullptr))
            {
                ec = get_last_win32_error();
                return std::nullopt;
            }

            if (bytesRead == 0)
                break;

            outIterator += bytesRead;
        }

        return result;
    }

    void
    store(std::filesystem::path const& path, std::span<std::byte> const& data, std::error_code& ec)
    {
        //
        // It's kind of too bad we can't do some cool transactional filesystem
        // magic
        //
        unique_hfile file{
            ::CreateFileW(path.c_str(),
                          GENERIC_WRITE,
                          0, // FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                          CREATE_ALWAYS, // vs. CREATE_NEW
                          0,
                          nullptr)};
        if (file.get() == INVALID_HANDLE_VALUE)
        {
            ec = get_last_win32_error();
            return;
        }

        std::size_t offset{};
        std::size_t remainingBytes{data.size_bytes()};

        while (offset < data.size())
        {
            //
            // arbitrary, but must be less than the maximum value a DWORD can
            // hold since that's the largest that WriteFile / WriteFileEx
            // can do. Perhaps there are other limits further down the I/O
            // stack, no need to challenge them. In practice, buffered writes
            // above 64kb at a time tend to suffice to keep the wasted time
            // in the noise range unless you're in a very high i/o scenario,
            // in which case you should not be using a synchronous API like
            // this one.
            //
            // If a Store variant is implemented that is async aware, it
            // perhaps should be smarter or tunable.
            //
            constexpr std::size_t writeChunkSize = 1ull << 20;
            static_assert(writeChunkSize < (std::numeric_limits<DWORD>::max)());
            DWORD bytesToWrite = m::to<DWORD>(std::min<std::size_t>(1ull << 20, remainingBytes));
            DWORD bytesWritten{};
            if (!::WriteFile(file.get(), &data[offset], bytesToWrite, &bytesWritten, nullptr))
            {
                ec = get_last_win32_error();
                return;
            }

            offset += bytesWritten;
            remainingBytes -= bytesWritten;
        }
    }

    void
    store(std::filesystem::path const& path, std::span<std::byte> const& data)
    {
        unique_hfile file{
            ::CreateFileW(path.c_str(),
                          GENERIC_WRITE,
                          0, // FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                          CREATE_ALWAYS, // vs. CREATE_NEW
                          0,
                          nullptr)};
        if (file.get() == INVALID_HANDLE_VALUE)
            throw_last_win32_error();

        std::size_t offset{};
        std::size_t remainingBytes{data.size_bytes()};

        while (offset < data.size())
        {
            //
            // arbitrary, but must be less than the maximum value a DWORD can
            // hold since that's the largest that WriteFile / WriteFileEx
            // can do. Perhaps there are other limits further down the I/O
            // stack, no need to challenge them. In practice, buffered writes
            // above 64kb at a time tend to suffice to keep the wasted time
            // in the noise range unless you're in a very high i/o scenario,
            // in which case you should not be using a synchronous API like
            // this one.
            //
            // If a Store variant is implemented that is async aware, it
            // perhaps should be smarter or tunable.
            //
            constexpr std::size_t writeChunkSize = 1ull << 20;
            static_assert(writeChunkSize < (std::numeric_limits<DWORD>::max)());
            DWORD bytesToWrite = m::to<DWORD>(std::min<std::size_t>(1ull << 20, remainingBytes));
            DWORD bytesWritten{};
            if (!::WriteFile(file.get(), &data[offset], bytesToWrite, &bytesWritten, nullptr))
                throw_last_win32_error();

            offset += bytesWritten;
            remainingBytes -= bytesWritten;
        }
    }

} // namespace m::filesystem
