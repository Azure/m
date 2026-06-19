// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <type_traits>
#include <variant>
#include <vector>

#include <m/mwin32/mWindows.h>
#include <m/pil/pil.h>

#include "session.h"

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

    //
    // The state behind a find-enumeration (mFindFirstFile / mFindNextFile)
    // pseudo-handle. A find handle does not name a single PIL node; it names a
    // position in a directory listing. The buffered entries are captured by the
    // mFindFirstFile call (via idirectory::enumerate_entries) and the cursor
    // advances on each mFindNextFile. Stored in the handle table behind a
    // shared_ptr so the variant holds shared_ptrs uniformly and the cursor
    // remains mutable through the dereferenced pointer.
    //
    struct find_enumeration_state
    {
        std::vector<m::pil::directory_entry> m_entries;
        std::size_t                          m_cursor = 0;
    };

    //
    // The iteration state behind a stream-find pseudo-handle (M-FS-STREAMS-2).
    // The stream names (including the leading colon and $DATA suffix) and sizes
    // are captured eagerly by mFindFirstStreamW via ifile::enumerate_streams;
    // mFindNextStreamW advances the cursor.
    //
    struct stream_enumeration_state
    {
        std::vector<m::pil::stream_entry> m_entries;
        std::size_t                       m_cursor = 0;
    };

    //
    // The per-directory change-notification watch behind a directory handle
    // (M-FS-NOTIFY-1). Defined out of line in mwinfile.cpp; only ever named here
    // through a shared_ptr, so a forward declaration suffices (the shared_ptr's
    // deleter is type-erased, so file_handle_state can be destroyed without the
    // complete type in scope). Held in file_handle_state so the watch (and the
    // PIL monitor token it owns) is released by RAII when the directory handle
    // is closed.
    //
    struct directory_watch_context;

    //
    // The state behind a file (mCreateFile) pseudo-handle. A minted file handle
    // resolves to its backing PIL ifile (the object every handle-based metadata
    // API queries) plus the path the client opened it with. The path is the
    // *public* path the caller passed to mCreateFile (pre-redirection): the
    // redirecting decorator maps public->private internally, so storing the
    // caller's path is exactly what mGetFinalPathNameByHandle must hand back
    // (private->public, D11) without any reverse-mapping support from the
    // provider. Stored behind a shared_ptr so the variant holds shared_ptrs
    // uniformly.
    //
    // A handle opened with FILE_FLAG_BACKUP_SEMANTICS names a directory (the
    // form ReadDirectoryChangesW requires); for such a handle m_file is null and
    // only m_path is meaningful. m_watch is lazily installed by the first
    // mReadDirectoryChangesW call on the handle and torn down when the handle
    // closes.
    //
    struct file_handle_state
    {
        std::shared_ptr<m::pil::ifile>           m_file;
        m::pil::file_path                        m_path;
        std::shared_ptr<directory_watch_context> m_watch;

        //
        // The sequential byte position consulted (and advanced) by mReadFile /
        // mWriteFile when the caller passes no explicit OVERLAPPED offset, and
        // set by mSetFilePointer / mSetFilePointerEx (M-FS-CONTENT). A handle
        // duplicated by mDuplicateHandle shares the same file_handle_state, so
        // the two handles share this position exactly as two Win32 handles onto
        // the same file object share one file pointer.
        //
        std::uint64_t m_position = 0;
    };

    class handle_table
    {
    public:
        handle_table();

        //
        // During process rundown (the DLL_PROCESS_DETACH the loader raises when
        // the process is terminating, as opposed to a FreeLibrary unload) the
        // destructor intentionally leaks the table instead of releasing its
        // payloads. See handle_table.cpp for the rationale and the DllMain that
        // arms the rundown flag this destructor consults.
        //
        ~handle_table();

        handle
        intern(std::shared_ptr<m::pil::ikey> const& sp);

        handle
        intern(std::shared_ptr<file_handle_state> const& sp);

        handle
        intern(std::shared_ptr<find_enumeration_state> const& sp);

        handle
        intern(std::shared_ptr<stream_enumeration_state> const& sp);

        template <typename T>
        T
        deref_handle(handle h)
        {
            //
            // Predefined registry pseudo-handles (HKEY_LOCAL_MACHINE, ...) are
            // never interned in the table; they resolve to their backing ikey
            // through the active session. Only the ikey variant alternative can
            // name a predefined key.
            //
            if constexpr (std::is_same_v<T, std::shared_ptr<m::pil::ikey>>)
            {
                if (auto sp = try_resolve_predefined_ikey(h.m_value))
                    return sp;
            }

            auto l = std::unique_lock(m_mutex);

            auto it = m_table.find(h.m_value);
            if (it == m_table.end())
                m::throw_win32_error_code(ERROR_INVALID_HANDLE);

            return std::get<T>(it->second.m_dv);
        }

        void
        close(handle h);

        //
        // True if `h`'s value matches the reserved minted-handle bit pattern
        // documented above (bit 30 set, bit 29 clear, bits 0-1 clear, no bits at
        // or above bit 31). This is a pure pattern test on the value; it does
        // not consult the table, so a match only means the value is in our
        // namespace, not that it is currently live. Used by the generic
        // mCloseHandle routing (M-FS-SHIM-6) to decide whether a handle belongs
        // to this table or to the real OS handle namespace.
        //
        static bool
        is_minted_handle_value(handle h) noexcept;

    private:
        using data_variant_type = std::variant<std::shared_ptr<m::pil::ikey>,
                                               std::shared_ptr<file_handle_state>,
                                               std::shared_ptr<find_enumeration_state>,
                                               std::shared_ptr<stream_enumeration_state>>;

        struct data
        {
            data_variant_type m_dv;
        };

        //
        // Shared minting loop for every interned alternative. Generates a fresh
        // handle value (per the bit-encoding above) and inserts the supplied
        // payload under it; the public intern overloads only differ in which
        // variant alternative they construct.
        //
        handle
        intern_variant(data_variant_type dv);

        std::mutex                m_mutex;
        std::random_device        m_rd;
        std::mt19937_64           m_mt;
        uintptr_t                 m_random_mask;
        uintptr_t                 m_counter;
        std::map<uintptr_t, data> m_table;
    };

} // namespace m::mwin32_impl

inline m::mwin32_impl::handle_table g_handles;
