// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define NOMINMAX

#include <cstdint>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/exception/exception.h>
#include <m/filesystem/filesystem_loadstore.h>
#include <m/formatters/Win32ErrorCode.h>
#include <m/tracing/tracing.h>

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

        std::vector<std::byte>
        load()
        {
            FILE_STANDARD_INFO fsi{};

            if (!::GetFileInformationByHandleEx(get(), FileStandardInfo, &fsi, sizeof(fsi)))
            {
                auto const last_error = ::GetLastError();
                m::wtrace_error(
                    L"Error on call to GetFileInformationByHandle {:#x} for FileStandardInfo: {}",
                    reinterpret_cast<uintptr_t>(get()),
                    fmtWin32ErrorCode{last_error});
                m::throw_win32_error_code(last_error);
            }

            uint64_t               file_size = fsi.EndOfFile.QuadPart;
            std::vector<std::byte> result(file_size);
            auto                   out_it           = result.begin();
            auto                   bytes_to_read    = m::to<DWORD>(std::min(1ull << 20, file_size));
            auto                   buffer_remaining = result.size();

            M_INTERNAL_ERROR_CHECK(result.size() == file_size);

            for (;;)
            {
                M_INTERNAL_ERROR_CHECK(buffer_remaining <= file_size);

                // Even if the file has grown, don't read through past the
                // end of the buffer.
                bytes_to_read =
                    m::to<DWORD>(std::min<std::size_t>(bytes_to_read, buffer_remaining));

                // If there's nothing left to read (empty file, or we've read
                // exactly file_size bytes), exit before dereferencing end().
                if (bytes_to_read == 0)
                    break;

                DWORD bytes_read{};

                if (!::ReadFile(get(), &*out_it, bytes_to_read, &bytes_read, nullptr))
                {
                    auto const last_error = ::GetLastError();
                    m::wtrace_error(L"Error on call to ReadFile {:#x} for {} bytes: {}",
                                    reinterpret_cast<uintptr_t>(get()),
                                    bytes_to_read,
                                    fmtWin32ErrorCode{last_error});
                    m::throw_win32_error_code(last_error);
                }

                if (bytes_read == 0)
                {
                    if (buffer_remaining != 0)
                    {
                        std::size_t actual_size = file_size - buffer_remaining;

                        m::wtrace(
                            m::tracing::event_kind::information,
                            L"File {:#x} found to be shorter during read than when first opened. {} vs. {} bytes",
                            reinterpret_cast<uintptr_t>(get()),
                            file_size,
                            actual_size);

                        result.resize(actual_size);
                    }
                    break;
                }

                M_INTERNAL_ERROR_CHECK(bytes_read <= buffer_remaining);
                M_INTERNAL_ERROR_CHECK(bytes_read <= bytes_to_read);

                out_it += bytes_read;
                buffer_remaining -= bytes_read;
            }

            return result;
        }

    private:
        HANDLE m_h;
    };
} // namespace

std::vector<std::byte>
m::filesystem::load(std::filesystem::path const& path)
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
        auto const last_error = ::GetLastError();
        wtrace_error(L"Error opening file {} for GENERIC_READ, FILE_SHARE_*, OPEN_EXISTING: {}",
                     path.c_str(),
                     fmtWin32ErrorCode{last_error});
        throw_win32_error_code(last_error);
    }

    return file.load();
}

std::optional<std::vector<std::byte>>
m::filesystem::load(std::filesystem::path const& path, std::error_code& ec)
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
        auto const last_error = ::GetLastError();
        wtrace_error(L"Error opening file {} for GENERIC_READ, FILE_SHARE_*, OPEN_EXISTING: {}",
                     path.c_str(),
                     fmtWin32ErrorCode{last_error});
        ec = make_win32_error_code(last_error);
        return std::nullopt;
    }

    return file.load();
}

void
m::filesystem::store(std::filesystem::path const&                    path,
                     std::span<std::byte const, std::dynamic_extent> data,
                     std::error_code&                                ec)
{
    //
    // It's kind of too bad we can't do some cool transactional filesystem
    // magic
    //
    unique_hfile file{::CreateFileW(path.c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    CREATE_ALWAYS, // vs. CREATE_NEW
                                    0,
                                    nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        auto const last_error = ::GetLastError();
        wtrace_error(L"Error opening file {} for GENERIC_WRITE, FILE_SHARE_*, CREATE_ALWAYS: {}",
                     path.c_str(),
                     fmtWin32ErrorCode{last_error});
        ec = make_win32_error_code(last_error);
        return;
    }

    std::size_t offset{};
    std::size_t remaining_bytes{data.size_bytes()};

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
        constexpr std::size_t write_chunk_size = 1ull << 20;
        static_assert(write_chunk_size < (std::numeric_limits<DWORD>::max)());
        DWORD bytes_to_write = m::to<DWORD>(std::min<std::size_t>(1ull << 20, remaining_bytes));
        DWORD bytes_written{};
        if (!::WriteFile(file.get(), &data[offset], bytes_to_write, &bytes_written, nullptr))
        {
            auto const last_error = ::GetLastError();
            m::wtrace_error(L"Error on call to WriteFile {:#x} for {} bytes: {}",
                            reinterpret_cast<uintptr_t>(file.get()),
                            bytes_to_write,
                            fmtWin32ErrorCode{last_error});
            ec = make_win32_error_code(last_error);
            return;
        }

        offset += bytes_written;
        remaining_bytes -= bytes_written;
    }
}

void
m::filesystem::store(std::filesystem::path const&                    path,
                     std::span<std::byte const, std::dynamic_extent> data)
{
    unique_hfile file{::CreateFileW(path.c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    CREATE_ALWAYS, // vs. CREATE_NEW
                                    0,
                                    nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        auto const last_error = ::GetLastError();
        wtrace_error(L"Error opening file {} for GENERIC_WRITE, FILE_SHARE_*, CREATE_ALWAYS: {}",
                     path.c_str(),
                     fmtWin32ErrorCode{last_error});
        throw_win32_error_code(last_error);
    }

    std::size_t offset{};
    std::size_t remaining_bytes{data.size_bytes()};

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
        constexpr std::size_t write_chunk_size = 1ull << 20;
        static_assert(write_chunk_size < (std::numeric_limits<DWORD>::max)());
        DWORD bytes_to_write = m::to<DWORD>(std::min<std::size_t>(1ull << 20, remaining_bytes));
        DWORD bytes_written{};
        if (!::WriteFile(file.get(), &data[offset], bytes_to_write, &bytes_written, nullptr))
        {
            auto const last_error = ::GetLastError();
            m::wtrace_error(L"Error on call to WriteFile {:#x} for {} bytes: {}",
                            reinterpret_cast<uintptr_t>(file.get()),
                            bytes_to_write,
                            fmtWin32ErrorCode{last_error});
            throw_win32_error_code(last_error);
        }

        offset += bytes_written;
        remaining_bytes -= bytes_written;
    }
}
