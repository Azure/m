// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/http_listener_interfaces.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/webcore_interfaces.h>

// Windows headers
#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include <http.h>

//
// Intercepting webcore decorator (D-HWC-4 opt-in, D-HWC-7).
//
// This decorator wraps an underlying direct `iwebcore` provider and interposes
// on `activate` to patch the loaded `hwebcore.dll` module's IAT, routing the
// engine's `Reg*` / `CreateFileW` / `FindFirstFileW` calls into the active PIL
// registry / filesystem surfaces.
//
// Unlike the materializing decorator (D-HWC-4 default), this approach does NOT
// project content to real paths — the engine's calls are intercepted and
// resolved against the isolated PIL surfaces directly. This enables:
//   1. Full isolation without temp-dir materialization.
//   2. Exact tracing of what the engine actually touched (logging facet).
//
// The interception is:
//   - Module-scoped: hooks are installed only on `hwebcore.dll`'s own IAT.
//   - Off by default: gated behind `webcore.interception` in `.pilcfg`.
//   - Reversible: hooks are uninstalled on instance destruction.
//
// Design notes: D-HWC-7 specifies that this is the bounded exception to the
// "no Detours" policy — we intercept *the engine's* calls only, never
// process-wide inline hooks.
//

namespace m::pil::impl::intercepting
{
    //--------------------------------------------------------------------------
    // iat_hook — a single IAT entry that was patched
    //--------------------------------------------------------------------------

    struct iat_hook
    {
        void**     iat_entry;      // address of the IAT slot we patched
        void*      original_func;  // original function pointer
        void*      hook_func;      // our replacement function pointer
        char const* function_name; // for debugging
    };

    //--------------------------------------------------------------------------
    // Synthetic HTTP queue for Tier B — fully deterministic HTTP (D-HWC-6)
    //--------------------------------------------------------------------------
    //
    // This queue enables in-process feeding of synthetic HTTP requests and
    // capturing of responses, bypassing http.sys entirely. The hooks check
    // synthetic_mode_enabled; when true, HttpReceiveHttpRequest returns
    // requests from the queue and HttpSendHttpResponse captures responses.
    //

    //
    // A synthetic HTTP request to be fed to the engine.
    // We store the raw bytes of method, URL, and headers; the hook marshals
    // them into HTTP_REQUEST.
    //
    struct synthetic_http_request
    {
        HTTP_REQUEST_ID                  request_id{0};
        std::string                      method;       // "GET", "POST", etc.
        std::wstring                     url;          // Full URL (http://host:port/path)
        std::vector<std::pair<std::string, std::string>> headers; // Header name/value pairs
        std::vector<std::uint8_t>        body;         // Request body
        std::uint16_t                    http_version_major{1};
        std::uint16_t                    http_version_minor{1};
    };

    //
    // A captured HTTP response from the engine.
    //
    struct captured_http_response
    {
        HTTP_REQUEST_ID                  request_id{0};
        std::uint16_t                    status_code{0};
        std::string                      reason_phrase;
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<std::uint8_t>        body;
        bool                             complete{false}; // True when all chunks received
    };

    //
    // Thread-safe synthetic HTTP request/response queue.
    //
    class synthetic_http_queue
    {
    public:
        //
        // A per-crossing observer (D-HWC-11). Invoked once, outside the queue
        // lock, when a response completes — delivering the originating request
        // paired with the captured response. This is how an activated edge taps
        // live traffic for validate-mode checking (D6: a side diagnostic that
        // never alters the engine). Observers must not throw.
        //
        using crossing_observer =
            std::function<void(synthetic_http_request const&, captured_http_response const&)>;

        synthetic_http_queue() = default;
        ~synthetic_http_queue() = default;

        // Non-copyable, non-movable.
        synthetic_http_queue(synthetic_http_queue const&) = delete;
        synthetic_http_queue& operator=(synthetic_http_queue const&) = delete;

        //
        // Enqueue a synthetic request for the engine to receive.
        // Returns the assigned request_id.
        //
        HTTP_REQUEST_ID
        enqueue_request(synthetic_http_request request);

        //
        // Try to dequeue a synthetic request (non-blocking).
        // Returns nullopt if the queue is empty.
        //
        std::optional<synthetic_http_request>
        try_dequeue_request();

        //
        // Put a previously dequeued request back at the front of the queue,
        // preserving its assigned request_id and FIFO position. Used when a
        // receive call could not marshal the request into the caller's buffer
        // (ERROR_MORE_DATA): the request must remain available for the retry
        // with a larger buffer rather than being lost.
        //
        void
        requeue_front(synthetic_http_request request);

        //
        // Dequeue a synthetic request (blocking with optional timeout).
        // Returns nullopt if timeout elapses without a request.
        //
        std::optional<synthetic_http_request>
        dequeue_request(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

        //
        // Capture a response from the engine for the given request_id.
        //
        void
        capture_response(HTTP_REQUEST_ID request_id, captured_http_response response);

        //
        // Append body data to an in-progress response.
        //
        void
        append_response_body(HTTP_REQUEST_ID request_id, std::span<std::uint8_t const> data);

        //
        // Mark a response as complete (all body data received).
        //
        void
        complete_response(HTTP_REQUEST_ID request_id);

        //
        // Get a captured response by request_id.
        // Returns nullopt if no response captured for that id.
        //
        std::optional<captured_http_response>
        get_response(HTTP_REQUEST_ID request_id) const;

        //
        // Wait for a response to be complete.
        // Returns nullopt if timeout elapses.
        //
        std::optional<captured_http_response>
        wait_for_response(HTTP_REQUEST_ID request_id, std::chrono::milliseconds timeout);

        //
        // Clear all pending requests and captured responses.
        //
        void
        clear();

        //
        // Check if there are pending requests.
        //
        bool
        has_pending_requests() const;

        //
        // Register a per-crossing observer (D-HWC-11). The observer fires for
        // every request whose response completes from registration onward.
        //
        void
        add_crossing_observer(crossing_observer observer);

    private:
        mutable std::mutex                                     m_mutex;
        std::condition_variable                                m_request_cv;
        std::condition_variable                                m_response_cv;

        std::deque<synthetic_http_request>                     m_pending_requests;
        std::unordered_map<HTTP_REQUEST_ID, captured_http_response> m_responses;

        // Requests handed to the engine (dequeued) but whose response has not
        // yet completed, keyed by request id. Retained so complete_response can
        // pair the originating request with its response for crossing observers.
        std::unordered_map<HTTP_REQUEST_ID, synthetic_http_request> m_inflight_requests;

        std::vector<crossing_observer>                         m_crossing_observers;

        HTTP_REQUEST_ID                                        m_next_request_id{1};
    };

    //--------------------------------------------------------------------------
    // interception_context — per-activation state for the hooks
    //--------------------------------------------------------------------------

    // Forward declaration.
    class webcore_instance;

    // Synthetic handle values (HKEY / HANDLE) are minted from high ranges that
    // real kernel handles never occupy. Each kind (keys, files, find cookies)
    // gets its own widely separated range so the per-kind counters — which only
    // ever increase — can never overtake each other and start aliasing another
    // kind's handles. A handle value below synthetic_handle_floor therefore
    // provably is not one of ours, which every file/handle hook uses as a
    // lock-free fast path. Changing any value is a breaking change to the
    // synthetic-handle scheme.
    inline constexpr uintptr_t synthetic_handle_floor      = 0x80000000;
    inline constexpr uintptr_t synthetic_key_handle_base   = 0x80000000;
    inline constexpr uintptr_t synthetic_file_handle_base  = 0xA0000000;
    inline constexpr uintptr_t synthetic_find_handle_base  = 0xC0000000;

    struct interception_context
    {
        // The PIL surfaces the hooks should route calls through.
        std::shared_ptr<iregistry>      registry;
        std::shared_ptr<ifilesystem>    filesystem;
        std::shared_ptr<ihttp_listener> http_listener;

        // HTTP listener session for URL remapping (D-HWC-6 Tier A).
        // Created during activation if endpoint mappings are configured.
        std::unique_ptr<ihttp_listener_session> http_listener_session;

        // Track remapped URLs: public URL -> private URL (for reverse lookup on removal).
        std::mutex                                          url_mapping_mutex;
        std::unordered_map<std::wstring, std::wstring>      public_to_private_url;
        std::unordered_map<std::wstring, std::wstring>      private_to_public_url;

        // Synthetic HTTP mode (D-HWC-6 Tier B).
        // When enabled, HttpReceiveHttpRequest returns requests from the queue
        // and HttpSendHttpResponse captures responses, bypassing http.sys entirely.
        bool                                                synthetic_http_enabled{false};
        std::unique_ptr<synthetic_http_queue>               synthetic_queue;

        // Handle table: maps HKEY values to PIL ikey smart pointers.
        // HKEY is an opaque handle; we synthesize unique values for intercepted
        // keys and map them back to the ikey when the engine makes calls.
        std::mutex                                     handle_mutex;
        std::unordered_map<HKEY, std::shared_ptr<ikey>> key_handles;
        HKEY                                            next_handle_value{reinterpret_cast<HKEY>(synthetic_key_handle_base)};

        // Handle table: maps HANDLE values to PIL ifile smart pointers plus the
        // per-handle current byte position (the Win32 file pointer advanced by
        // ReadFile / WriteFile / SetFilePointer on a handle opened without
        // FILE_FLAG_OVERLAPPED).
        struct file_state
        {
            std::shared_ptr<ifile> file;
            std::uint64_t          position{0};

            // Pending whole-file content assembled from WriteFile calls. The
            // backing ifile models only whole-file replacement at offset 0
            // (D16/D17), so positioned / chunked writes accumulate here and are
            // flushed as a single write_content on flush or close. While `dirty`
            // is set this buffer -- not the backing file -- is the authoritative
            // content for reads and size queries on this handle.
            std::vector<std::byte> write_buffer;
            bool                   dirty{false};
        };
        std::mutex                                      file_handle_mutex;
        std::unordered_map<HANDLE, file_state>          file_handles;
        HANDLE                                           next_file_handle_value{reinterpret_cast<HANDLE>(synthetic_file_handle_base)};

        // Handle table: maps HANDLE values to find-file state (directory enumeration).
        struct find_state
        {
            std::shared_ptr<idirectory>     directory;
            std::vector<directory_entry>    entries;
            std::size_t                     current_index{0};
        };
        std::mutex                                        find_handle_mutex;
        std::unordered_map<HANDLE, find_state>            find_handles;
        HANDLE                                            next_find_handle_value{reinterpret_cast<HANDLE>(synthetic_find_handle_base)};

        // Allocate a synthetic HKEY handle for a PIL key.
        HKEY
        allocate_key_handle(std::shared_ptr<ikey> const& key)
        {
            std::lock_guard<std::mutex> guard(handle_mutex);
            HKEY h = next_handle_value;
            next_handle_value = reinterpret_cast<HKEY>(
                reinterpret_cast<uintptr_t>(next_handle_value) + 1);
            key_handles[h] = key;
            return h;
        }

        // Look up a PIL key from a synthetic HKEY handle.
        std::shared_ptr<ikey>
        lookup_key_handle(HKEY hkey) const
        {
            std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(handle_mutex));
            auto it = key_handles.find(hkey);
            if (it == key_handles.end())
                return nullptr;
            return it->second;
        }

        // Release a synthetic HKEY handle.
        bool
        release_key_handle(HKEY hkey)
        {
            std::lock_guard<std::mutex> guard(handle_mutex);
            return key_handles.erase(hkey) > 0;
        }

        // Allocate a synthetic HANDLE for a PIL file.
        HANDLE
        allocate_file_handle(std::shared_ptr<ifile> const& file)
        {
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            HANDLE h = next_file_handle_value;
            next_file_handle_value = reinterpret_cast<HANDLE>(
                reinterpret_cast<uintptr_t>(next_file_handle_value) + 1);
            file_handles[h] = file_state{file, 0};
            return h;
        }

        // Look up a PIL file from a synthetic HANDLE.
        std::shared_ptr<ifile>
        lookup_file_handle(HANDLE handle) const
        {
            std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(file_handle_mutex));
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return nullptr;
            return it->second.file;
        }

        // Is `handle` one of our synthetic file handles?
        bool
        is_synthetic_file_handle(HANDLE handle) const
        {
            std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(file_handle_mutex));
            return file_handles.find(handle) != file_handles.end();
        }

        // Read from a synthetic file handle at its current position, advancing
        // the position by the count read. Returns false (without touching the
        // out-params) if `handle` is not one of ours, so the caller can fall
        // through to the real ReadFile. The backing read runs *without* the
        // handle lock held, so reads on independent files do not serialise on a
        // single mutex (the lock is taken only to snapshot the file pointer and
        // again to advance it).
        bool
        read_file_handle(HANDLE               handle,
                         std::span<std::byte> buffer,
                         std::size_t&         bytes_read,
                         std::error_code&     ec)
        {
            bytes_read = 0;
            ec.clear();

            std::shared_ptr<ifile> file;
            std::uint64_t          off = 0;
            {
                std::lock_guard<std::mutex> guard(file_handle_mutex);
                auto it = file_handles.find(handle);
                if (it == file_handles.end())
                    return false;
                auto& st = it->second;
                off      = st.position;

                // While dirty the in-memory buffer is the authoritative content;
                // serve the read from it directly (a fast in-lock memcpy).
                if (st.dirty)
                {
                    if (off < st.write_buffer.size())
                    {
                        std::size_t const avail =
                            st.write_buffer.size() - static_cast<std::size_t>(off);
                        std::size_t const n = (std::min)(avail, buffer.size());
                        std::memcpy(buffer.data(),
                                    st.write_buffer.data() + off, n);
                        bytes_read  = n;
                        st.position = off + n;
                    }
                    return true;
                }

                if (!st.file)
                {
                    ec = std::make_error_code(std::errc::bad_file_descriptor);
                    return true;
                }
                file = st.file; // snapshot for the unlocked read
            }

            // Slow path: read the backing file without holding the lock.
            file->read_content(ifile::read_content_flags{}, off, buffer,
                               bytes_read, ec);
            if (ec)
                return true;

            // Re-acquire briefly to advance the position (the entry may have been
            // closed concurrently, in which case there is nothing to update).
            {
                std::lock_guard<std::mutex> guard(file_handle_mutex);
                auto it = file_handles.find(handle);
                if (it != file_handles.end())
                    it->second.position = off + bytes_read;
            }
            return true;
        }

        // Write to a synthetic file handle at its current position. Writes
        // accumulate into an in-memory whole-file buffer (the authoritative
        // content while the handle is dirty) and are flushed to the backing
        // ifile as a single whole-file write_content on flush or close, so the
        // engine can write a file in multiple chunks even though write_content
        // models only whole-file replacement. Returns false if `handle` is not
        // one of ours.
        bool
        write_file_handle(HANDLE                     handle,
                          std::span<std::byte const> buffer,
                          std::size_t&               bytes_written,
                          std::error_code&           ec)
        {
            bytes_written = 0;
            ec.clear();
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            if (!st.file)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return true;
            }
            std::uint64_t const off = st.position;
            std::size_t const   end = static_cast<std::size_t>(off) + buffer.size();
            if (st.write_buffer.size() < end)
                st.write_buffer.resize(end); // zero-fill any gap below `off`
            if (!buffer.empty())
                std::memcpy(st.write_buffer.data() + off, buffer.data(),
                            buffer.size());
            st.dirty      = true;
            st.position   = end;
            bytes_written = buffer.size();
            return true;
        }

        // Flush any pending writes on a synthetic file handle to the backing
        // ifile (a single whole-file write_content). Returns false if `handle`
        // is not one of ours; a clean (non-dirty) handle succeeds as a no-op.
        bool
        flush_file_handle(HANDLE handle, std::error_code& ec)
        {
            ec.clear();
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            if (!st.dirty)
                return true;
            if (!st.file)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return true;
            }
            std::size_t written = 0;
            st.file->write_content(ifile::write_content_flags{}, 0,
                                   std::span<std::byte const>(st.write_buffer),
                                   written, ec);
            if (!ec)
                st.dirty = false;
            return true;
        }

        // Set the synthetic file's logical end to the handle's current position
        // (the Win32 SetEndOfFile contract). The truncated/extended content
        // becomes pending and is flushed on the next flush / close. Returns false
        // if `handle` is not one of ours. (Truncating an as-yet-unwritten file to
        // a non-zero length writes a zero-filled prefix on flush, an accepted
        // limitation of the whole-file write model.)
        bool
        set_end_of_file_handle(HANDLE handle, std::error_code& ec)
        {
            ec.clear();
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            st.write_buffer.resize(static_cast<std::size_t>(st.position));
            st.dirty = true;
            return true;
        }

        // Query the byte length of a synthetic file handle's content. Returns
        // false if `handle` is not one of ours. A failed metadata query is
        // surfaced through `ec` rather than reported as size 0.
        bool
        get_file_handle_size(HANDLE handle, std::uint64_t& size, std::error_code& ec)
        {
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            size = 0;
            ec.clear();
            if (st.dirty)
            {
                size = st.write_buffer.size();
                return true;
            }
            if (!st.file)
            {
                ec = std::make_error_code(std::errc::bad_file_descriptor);
                return true;
            }
            file_metadata md;
            auto const d =
                st.file->query_information(ifile::query_information_flags{}, md);
            if (d)
            {
                ec = std::make_error_code(std::errc::io_error);
                return true;
            }
            size = md.m_size;
            return true;
        }

        // Reposition a synthetic file handle's pointer (FILE_BEGIN /
        // FILE_CURRENT / FILE_END). Returns false if `handle` is not one of ours.
        // A failed metadata query on the FILE_END path is surfaced through `ec`.
        bool
        set_file_handle_pointer(HANDLE           handle,
                                std::int64_t     distance,
                                DWORD            move_method,
                                std::uint64_t&   new_position,
                                std::error_code& ec)
        {
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            ec.clear();
            std::int64_t base = 0;
            switch (move_method)
            {
            case FILE_BEGIN:
                base = 0;
                break;
            case FILE_CURRENT:
                base = static_cast<std::int64_t>(st.position);
                break;
            case FILE_END:
                if (st.dirty)
                {
                    base = static_cast<std::int64_t>(st.write_buffer.size());
                }
                else if (st.file)
                {
                    file_metadata md;
                    auto const    d = st.file->query_information(
                        ifile::query_information_flags{}, md);
                    if (d)
                    {
                        ec = std::make_error_code(std::errc::io_error);
                        return true;
                    }
                    base = static_cast<std::int64_t>(md.m_size);
                }
                break;
            default:
                ec = std::make_error_code(std::errc::invalid_argument);
                return true;
            }
            std::int64_t const target = base + distance;
            if (target < 0)
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return true;
            }
            st.position  = static_cast<std::uint64_t>(target);
            new_position = st.position;
            return true;
        }

        // Release a synthetic file HANDLE.
        bool
        release_file_handle(HANDLE handle)
        {
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            return file_handles.erase(handle) > 0;
        }

        // Flush pending writes and release a synthetic file HANDLE in one locked
        // step (the CloseHandle path). Returns true if `handle` was one of ours
        // (whether or not the flush succeeded); any flush error is reported
        // through `ec`.
        bool
        close_file_handle(HANDLE handle, std::error_code& ec)
        {
            ec.clear();
            std::lock_guard<std::mutex> guard(file_handle_mutex);
            auto it = file_handles.find(handle);
            if (it == file_handles.end())
                return false;
            auto& st = it->second;
            if (st.dirty && st.file)
            {
                std::size_t written = 0;
                st.file->write_content(ifile::write_content_flags{}, 0,
                                       std::span<std::byte const>(st.write_buffer),
                                       written, ec);
            }
            file_handles.erase(it);
            return true;
        }

        // Allocate a synthetic HANDLE for find-file state.
        HANDLE
        allocate_find_handle(find_state state)
        {
            std::lock_guard<std::mutex> guard(find_handle_mutex);
            HANDLE h = next_find_handle_value;
            next_find_handle_value = reinterpret_cast<HANDLE>(
                reinterpret_cast<uintptr_t>(next_find_handle_value) + 1);
            find_handles[h] = std::move(state);
            return h;
        }

        // Look up find-file state from a synthetic HANDLE.
        find_state*
        lookup_find_handle(HANDLE handle)
        {
            std::lock_guard<std::mutex> guard(find_handle_mutex);
            auto it = find_handles.find(handle);
            if (it == find_handles.end())
                return nullptr;
            return &it->second;
        }

        // Release a synthetic find HANDLE.
        bool
        release_find_handle(HANDLE handle)
        {
            std::lock_guard<std::mutex> guard(find_handle_mutex);
            return find_handles.erase(handle) > 0;
        }

        // Record a URL remapping (public -> private) for later reverse lookup.
        void
        record_url_mapping(std::wstring const& public_url, std::wstring const& private_url)
        {
            std::lock_guard<std::mutex> guard(url_mapping_mutex);
            public_to_private_url[public_url] = private_url;
            private_to_public_url[private_url] = public_url;
        }

        // Look up the private URL for a given public URL.
        std::optional<std::wstring>
        lookup_private_url(std::wstring const& public_url) const
        {
            std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(url_mapping_mutex));
            auto it = public_to_private_url.find(public_url);
            if (it == public_to_private_url.end())
                return std::nullopt;
            return it->second;
        }

        // Look up the public URL for a given private URL.
        std::optional<std::wstring>
        lookup_public_url(std::wstring const& private_url) const
        {
            std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(url_mapping_mutex));
            auto it = private_to_public_url.find(private_url);
            if (it == private_to_public_url.end())
                return std::nullopt;
            return it->second;
        }

        // Remove a URL mapping by public URL.
        bool
        remove_url_mapping_by_public(std::wstring const& public_url)
        {
            std::lock_guard<std::mutex> guard(url_mapping_mutex);
            auto it = public_to_private_url.find(public_url);
            if (it == public_to_private_url.end())
                return false;
            std::wstring private_url = it->second;
            public_to_private_url.erase(it);
            private_to_public_url.erase(private_url);
            return true;
        }
    };

    //--------------------------------------------------------------------------
    // intercepting_webcore_instance — RAII token with hook cleanup
    //--------------------------------------------------------------------------

    class webcore_instance final : public iwebcore_instance
    {
    public:
        webcore_instance() = delete;
        webcore_instance(webcore_instance const&) = delete;
        webcore_instance(webcore_instance&&) = delete;
        webcore_instance& operator=(webcore_instance const&) = delete;
        webcore_instance& operator=(webcore_instance&&) = delete;

        // Constructs the RAII token. Takes ownership of the underlying instance,
        // the interception context that backs the installed hooks, and the list
        // of hooks to uninstall on destruction.
        webcore_instance(std::unique_ptr<iwebcore_instance>    underlying_instance,
                         std::unique_ptr<interception_context> context,
                         HMODULE                               target_module,
                         std::vector<iat_hook>                 installed_hooks);

        ~webcore_instance() override;

        //
        // The activation's in-process synthetic-HTTP edge (D-HWC-11), or null
        // when synthetic mode was not enabled for this activation. The returned
        // edge adapts the public contract message types onto this instance's
        // internal synthetic_http_queue.
        //
        isynthetic_http_edge*
        synthetic_http_edge() override;

    private:
        std::unique_ptr<iwebcore_instance>    m_underlying_instance;
        HMODULE                               m_target_module;
        std::vector<iat_hook>                 m_installed_hooks;
        std::unique_ptr<interception_context> m_context;

        // The public-typed edge adapter over m_context->synthetic_queue, created
        // at construction iff synthetic mode is enabled. Declared after m_context
        // so it is destroyed before the queue it points into.
        std::unique_ptr<isynthetic_http_edge> m_synthetic_edge;
    };

    //--------------------------------------------------------------------------
    // intercepting_webcore — decorator that intercepts engine API calls
    //--------------------------------------------------------------------------

    class webcore final : public iwebcore, public std::enable_shared_from_this<webcore>
    {
    public:
        // Construct with references to the PIL platform and the underlying
        // (direct) webcore provider. The platform provides the registry and
        // filesystem surfaces the hooks will route calls through.
        webcore(std::shared_ptr<iplatform> platform,
                std::shared_ptr<iwebcore>  underlying_webcore);

        webcore(webcore const&) = delete;
        webcore(webcore&&) = delete;
        webcore& operator=(webcore const&) = delete;
        webcore& operator=(webcore&&) = delete;

        ~webcore() override = default;

        // iwebcore interface

        activate_disposition
        activate(activate_flags                      flags,
                 activation_request const&           request,
                 std::unique_ptr<iwebcore_instance>& returned_instance,
                 std::error_code&                    ec) override;

        set_metadata_disposition
        set_metadata(set_metadata_flags  flags,
                     std::u16string_view type,
                     std::u16string_view value,
                     std::error_code&    ec) override;

    private:
        // Walk the module's IAT and patch the specified imports.
        std::vector<iat_hook>
        install_iat_hooks(HMODULE target_module);

        // Restore the original IAT entries.
        static void
        uninstall_iat_hooks(HMODULE target_module, std::vector<iat_hook> const& hooks);

        std::mutex               m_mutex;
        std::shared_ptr<iplatform> m_platform;
        std::shared_ptr<iwebcore>  m_underlying_webcore;
    };

    //--------------------------------------------------------------------------
    // Factory function
    //--------------------------------------------------------------------------

    std::shared_ptr<iwebcore>
    create_intercepting_webcore(std::shared_ptr<iplatform> platform,
                                std::shared_ptr<iwebcore>  underlying_webcore);

    //--------------------------------------------------------------------------
    // Global interception context — set during activation, used by hooks
    //--------------------------------------------------------------------------
    //
    // Note: This is a plain process-global, NOT thread_local. The engine's
    // hooks fire on hwebcore.dll's own worker/async threads, which a
    // thread_local pointer would never reach. The pointer is held in a
    // std::atomic (read with acquire, written with release); each hook reads it
    // through the active_context() accessor. See the definition of the cell in
    // intercepting_webcore.cpp for the full memory-ordering rationale.
    //

    extern std::atomic<interception_context*> g_active_context_cell;

    inline interception_context*
    active_context() noexcept
    {
        return g_active_context_cell.load(std::memory_order_acquire);
    }

} // namespace m::pil::impl::intercepting
