// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <variant>

#include <m/mwin32/mWindows.h>
#include <m/pil/pil.h>

namespace m::mwin32_impl
{
    //
    // This implements a handle table, somewhat akin to the win32 handle
    // table.
    //
    // The initial iteration is focused more on easy to use function,
    // rather than necessarily performance or efficiency. Future iterations
    // should focus on these aspects if they are important.
    //
    // A few derivable tidbits of information about Win32 handles:
    //
    // - Because they can be duplicated between 32 and 64 bit processes, they
    //      are de-facto constrained to 32 bits in size. In theory on a future
    //      64 bit only Windows SKU they could be opened up to be larger, but
    //      it's unclear when there will be a need for more handles than can
    //      be encoded into the 32 bit handles.
    //
    // - The bottom two bits are very commonly used by clients and so are
    //      never assigned by the operating system. It's not entirely clear
    //      if they are ignored or reserved, so we will treat them as
    //      MBZ for creation, ignored on consumption.
    //
    // - The 31st bit (top bit for 32bit machines) is never used in a valid
    //      handle
    //
    // - The effect of all this is that by casual inspection, there cannot be
    //      more than 29 bits usable in a handle.
    //
    // - In practice, because of the metadata required for handles, the
    //      practical limit on handles, even in a 64 bit process, is much
    //      lower. At some point, the handle table will dominate the
    //      process's address space.
    //
    // => As such, we will mark our "handles" as following:
    //
    //      Bits 31 .. max: MUST BE 0
    //      Bit 30:         MUST BE 1
    //      Bit 29:         MUST BE 0
    //      Bits 2 .. 28:   a sequence number used to map into the
    //          global handle table
    //      Bits 0 .. 1:    MUST BE 0
    //
    // (Just in case that was said wrongly, for a 64 bit handle, the
    // top 32 bits are clear, the top bit is clear, the next bit is set,
    // the next bit is clear, and the bottom two bits are clear. For a 32
    // bit handle, the top bit is clear, the next bit is set, the next
    // bit is clear, and the bottom two bits are clear.)
    //
    // 64 bit handle:
    //
    // 00000000'00000000'00000000'00000000'010xxxxx'xxxxxxxx'xxxxxxxx'xxxxxx00
    //
    // 32 bit handle:
    //
    // 010xxxxx'xxxxxxxx'xxxxxxxx'xxxxxx00
    //
    // (same diagram with the top 32 bits lopped off)
    //
    // Hopefully this will generate handles that will appear normally
    // valid, not confuse "most" naive handle-based code, even if it
    // were to store it in a 32 bit value, and never conflict in practice
    // with an actual Win32 handle value.
    //
    // This gives 2^27 handle values which frankly should be plenty. I am
    // considering using the top 2-3 bits as a checksum/parity.
    //

    class handle_table;

    /// <summary>
    /// The `handle` class is effectively a POD where the data is private.
    /// </summary>
    class handle
    {
    public:
        handle() = default;
        constexpr handle(handle const& other): m_value(other.m_value) {}
        ~handle() = default;

        constexpr handle&
        operator=(handle const& other)
        {
            m_value = other.m_value;
            return *this;
        }

        constexpr void
        swap(handle& other) noexcept
        {
            using std::swap;
            swap(m_value, other.m_value);
        }

        HANDLE
        as_HANDLE() const;

        HKEY
        as_HKEY() const;

        static handle
        from_HANDLE(HANDLE h);

        static handle
        from_HKEY(HKEY hkey);

    private:
        constexpr handle(uintptr_t value) noexcept: m_value(value) {}

        uintptr_t m_value{};

        friend class handle_table;
    };

    class handle_table
    {
    public:
        handle_table();

        handle
        intern(std::shared_ptr<m::pil::ikey> const& sp);

        template <typename T>
        T
        deref_handle(handle h)
        {
            auto l = std::unique_lock(m_mutex);

            auto it = m_table.find(h.m_value);
            if (it == m_table.end())
                m::throw_win32_error_code(ERROR_INVALID_HANDLE);

            return std::get<T>(it->second.m_dv);
        }

        void
        close(handle h);

    private:
        using data_variant_type = std::variant<std::shared_ptr<m::pil::ikey>>;

        struct data
        {
            data_variant_type m_dv;
        };

        std::mutex                m_mutex;
        std::random_device        m_rd;
        std::mt19937_64           m_mt;
        uintptr_t                 m_random_mask;
        uintptr_t                 m_counter;
        std::map<uintptr_t, data> m_table;
    };

} // namespace m::mwin32_impl

inline m::mwin32_impl::handle_table g_handles;
