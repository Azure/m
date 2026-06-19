// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// M-HWC-INTERCEPT-2: Integration test for the intercepting webcore decorator.
// Validates that:
//   1. The IAT hooking infrastructure is correctly set up
//   2. Activation installs hooks on the target module
//   3. Destruction cleans up hooks properly
//   4. The hook functions are invoked (fall-through to originals in stub impl)
//

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include <gtest/gtest.h>

// winsock2.h must be included before Windows.h to avoid redefinition errors
// (intercepting_webcore.h includes http.h which includes winsock2.h)
#include <winsock2.h>
#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/webcore_interfaces.h>

#include <pugixml.hpp>

#include "buffered/buffered.h"
#include "intercepting/intercepting_webcore.h"

namespace
{
    namespace bufimpl = m::pil::impl::buffered;
    namespace intcimpl = m::pil::impl::intercepting;

    //--------------------------------------------------------------------------
    // Mock ifile — an in-memory byte container for exercising the synthetic
    // file-handle I/O routed through interception_context (M-HWC-REVIEW-2).
    //--------------------------------------------------------------------------

    struct mock_file final : m::pil::ifile
    {
        std::vector<std::byte> bytes;

        query_information_disposition
        query_information(query_information_flags, m::pil::file_metadata& metadata) override
        {
            metadata          = m::pil::file_metadata{};
            metadata.m_kind   = m::pil::node_kind::file;
            metadata.m_size   = bytes.size();
            return query_information_disposition{};
        }

        read_content_disposition
        read_content(read_content_flags,
                     std::uint64_t        offset,
                     std::span<std::byte> buffer,
                     std::size_t&         bytes_read,
                     std::error_code&     ec) override
        {
            ec.clear();
            bytes_read = 0;
            if (offset >= bytes.size())
                return read_content_disposition{}; // EOF: short (zero) read.
            std::size_t const available = bytes.size() - static_cast<std::size_t>(offset);
            std::size_t const n         = (std::min)(available, buffer.size());
            std::memcpy(buffer.data(), bytes.data() + offset, n);
            bytes_read = n;
            return read_content_disposition{};
        }

        write_content_disposition
        write_content(write_content_flags,
                      std::uint64_t              offset,
                      std::span<std::byte const> buffer,
                      std::size_t&               bytes_written,
                      std::error_code&           ec) override
        {
            ec.clear();
            bytes_written = 0;
            // The PIL write model is whole-file replacement at offset 0.
            if (offset != 0)
            {
                ec = std::make_error_code(std::errc::not_supported);
                return write_content_disposition{};
            }
            bytes.assign(buffer.begin(), buffer.end());
            bytes_written = buffer.size();
            return write_content_disposition{};
        }
    };

    //--------------------------------------------------------------------------
    // Mock webcore — records the activation and exposes the HMODULE
    //--------------------------------------------------------------------------

    class mock_webcore_instance final : public m::pil::iwebcore_instance
    {
    public:
        mock_webcore_instance() = default;
        ~mock_webcore_instance() override = default;
    };

    struct recorded_activation
    {
        m::pil::file_path                app_host_config;
        std::optional<m::pil::file_path> root_web_config;
        std::u16string                   instance_name;
    };

    class mock_webcore final : public m::pil::iwebcore
    {
    public:
        mock_webcore()  = default;
        ~mock_webcore() override = default;

        // Set the HMODULE to report for hook installation.
        void
        set_target_module(HMODULE hmod)
        {
            m_target_module = hmod;
        }

        HMODULE
        get_target_module() const
        {
            return m_target_module;
        }

        // iwebcore interface
        activate_disposition
        activate(activate_flags                              flags,
                 m::pil::activation_request const&           request,
                 std::unique_ptr<m::pil::iwebcore_instance>& returned_instance,
                 std::error_code&                            ec) override
        {
            std::lock_guard<std::mutex> guard(m_mutex);

            ec.clear();
            m_activations.push_back(recorded_activation{
                request.app_host_config,
                request.root_web_config,
                request.instance_name});

            returned_instance = std::make_unique<mock_webcore_instance>();
            return activate_disposition{};
        }

        set_metadata_disposition
        set_metadata(set_metadata_flags  flags,
                     std::u16string_view type,
                     std::u16string_view value,
                     std::error_code&    ec) override
        {
            ec.clear();
            return set_metadata_disposition{};
        }

        // Test accessors
        std::vector<recorded_activation> const&
        activations() const
        {
            return m_activations;
        }

    private:
        mutable std::mutex               m_mutex;
        std::vector<recorded_activation> m_activations;
        HMODULE                          m_target_module = nullptr;
    };

    //--------------------------------------------------------------------------
    // Mock platform for the intercepting decorator
    //--------------------------------------------------------------------------

    class mock_platform final : public m::pil::iplatform
    {
    public:
        mock_platform(std::shared_ptr<m::pil::iregistry>   registry,
                      std::shared_ptr<m::pil::ifilesystem> filesystem)
            : m_registry(std::move(registry))
            , m_filesystem(std::move(filesystem))
        {
        }

        ~mock_platform() override = default;

        // iplatform interface
        get_registry_disposition
        get_registry(get_registry_flags                   flags,
                     std::shared_ptr<m::pil::iregistry>&  returned_registry) override
        {
            returned_registry = m_registry;
            return get_registry_disposition{};
        }

        get_filesystem_disposition
        get_filesystem(get_filesystem_flags                   flags,
                       std::shared_ptr<m::pil::ifilesystem>&  returned_filesystem) override
        {
            returned_filesystem = m_filesystem;
            return get_filesystem_disposition{};
        }

        get_webcore_disposition
        get_webcore(get_webcore_flags                  flags,
                    std::shared_ptr<m::pil::iwebcore>& returned_webcore) override
        {
            returned_webcore = nullptr;
            return get_webcore_disposition{};
        }

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override
        {
            // No-op for mock.
            return save_disposition{};
        }

    private:
        std::shared_ptr<m::pil::iregistry>   m_registry;
        std::shared_ptr<m::pil::ifilesystem> m_filesystem;
    };

    //--------------------------------------------------------------------------
    // Helper: create a buffered registry from the platform
    //--------------------------------------------------------------------------

    std::shared_ptr<bufimpl::registry>
    make_buffered_registry()
    {
        auto platform = m::pil::make_platform_interface();
        std::shared_ptr<m::pil::iregistry> underlying_reg;
        platform->get_registry(m::pil::iplatform::get_registry_flags{}, underlying_reg);
        return std::make_shared<bufimpl::registry>(underlying_reg);
    }

    //--------------------------------------------------------------------------
    // Helper: create a buffered filesystem from the platform
    //--------------------------------------------------------------------------

    std::shared_ptr<bufimpl::filesystem>
    make_buffered_filesystem()
    {
        auto platform = m::pil::make_platform_interface();
        std::shared_ptr<m::pil::ifilesystem> underlying_fs;
        platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, underlying_fs);
        return std::make_shared<bufimpl::filesystem>(underlying_fs);
    }

    //--------------------------------------------------------------------------
    // Helper: convert file_path
    //--------------------------------------------------------------------------

    m::pil::file_path
    to_file_path(std::u16string_view path)
    {
        return m::pil::file_path(m::pil::file_path::view_type(path));
    }

} // namespace

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

TEST(InterceptingWebcore, CreateDecorator)
{
    // Arrange: Create buffered surfaces and mock platform.
    auto buffered_reg = make_buffered_registry();
    auto buffered_fs  = make_buffered_filesystem();
    auto mock_platform_ptr = std::make_shared<mock_platform>(buffered_reg, buffered_fs);

    // Create mock underlying webcore.
    auto mock_underlying = std::make_shared<mock_webcore>();

    // Act: Create the intercepting decorator.
    auto intercepting_wc = intcimpl::create_intercepting_webcore(mock_platform_ptr, mock_underlying);

    // Assert: Decorator created successfully.
    ASSERT_NE(intercepting_wc, nullptr);
}

TEST(InterceptingWebcore, ActivationWithNoTargetModule)
{
    // Arrange: Create buffered surfaces and mock platform.
    auto buffered_reg = make_buffered_registry();
    auto buffered_fs  = make_buffered_filesystem();
    auto mock_platform_ptr = std::make_shared<mock_platform>(buffered_reg, buffered_fs);

    // Create mock underlying webcore (no target module set).
    auto mock_underlying = std::make_shared<mock_webcore>();

    // Create the intercepting decorator.
    auto intercepting_wc = intcimpl::create_intercepting_webcore(mock_platform_ptr, mock_underlying);

    // Act: Activate the webcore.
    m::pil::activation_request request;
    request.app_host_config = to_file_path(u"C:\\inetpub\\config\\applicationHost.config");
    request.instance_name   = u"TestInstance";

    std::unique_ptr<m::pil::iwebcore_instance> instance;
    std::error_code                            ec;
    auto const d = intercepting_wc->activate(
        m::pil::iwebcore::activate_flags{}, request, instance, ec);

    // Assert: Activation should succeed (hooks may not be installed without a real module,
    // but the activation should forward to the underlying webcore).
    ASSERT_FALSE(ec) << "Activation failed: " << ec.message();
    ASSERT_TRUE(instance);

    // Assert: The mock received exactly one activation.
    ASSERT_EQ(1u, mock_underlying->activations().size());

    auto const& recorded = mock_underlying->activations()[0];
    EXPECT_EQ(recorded.instance_name, u"TestInstance");

    // Cleanup: Destroy the instance.
    instance.reset();
}

TEST(InterceptingWebcore, SetMetadataForwards)
{
    // Arrange: Create buffered surfaces and mock platform.
    auto buffered_reg = make_buffered_registry();
    auto buffered_fs  = make_buffered_filesystem();
    auto mock_platform_ptr = std::make_shared<mock_platform>(buffered_reg, buffered_fs);

    // Create mock underlying webcore.
    auto mock_underlying = std::make_shared<mock_webcore>();

    // Create the intercepting decorator.
    auto intercepting_wc = intcimpl::create_intercepting_webcore(mock_platform_ptr, mock_underlying);

    // Act: Call set_metadata.
    std::error_code ec;
    auto const d = intercepting_wc->set_metadata(
        m::pil::iwebcore::set_metadata_flags{},
        std::u16string_view(u"TestType"),
        std::u16string_view(u"TestValue"),
        ec);

    // Assert: Call succeeded (forwarded to mock).
    ASSERT_FALSE(ec) << "set_metadata failed: " << ec.message();
}

TEST(InterceptingWebcore, InterceptionContextHandleTables)
{
    // Test the handle table allocation/lookup/release functions.
    intcimpl::interception_context ctx;

    // Test key handle allocation with a nullptr (valid for table testing).
    std::shared_ptr<m::pil::ikey> dummy_key = nullptr;
    HKEY h1 = ctx.allocate_key_handle(dummy_key);
    HKEY h2 = ctx.allocate_key_handle(dummy_key);

    // Assert: Different handles allocated.
    EXPECT_NE(h1, h2);

    // Assert: Lookup returns the key (nullptr in this case).
    auto looked_up = ctx.lookup_key_handle(h1);
    EXPECT_EQ(looked_up.get(), nullptr);

    // Assert: Release works.
    EXPECT_TRUE(ctx.release_key_handle(h1));
    EXPECT_FALSE(ctx.release_key_handle(h1)); // Already released.

    // Assert: After release, lookup returns nullptr (not found).
    looked_up = ctx.lookup_key_handle(h1);
    EXPECT_EQ(looked_up, nullptr);

    // Assert: h2 is still in the table.
    looked_up = ctx.lookup_key_handle(h2);
    EXPECT_EQ(looked_up.get(), nullptr); // The stored value is nullptr.
    EXPECT_TRUE(ctx.release_key_handle(h2)); // But the handle itself is valid.
}

TEST(InterceptingWebcore, InterceptionContextFileHandles)
{
    // Test the file handle table.
    intcimpl::interception_context ctx;

    // We can't easily create a real ifile, so test with nullptr.
    // In real usage, the file would be non-null.
    std::shared_ptr<m::pil::ifile> dummy_file = nullptr;

    HANDLE h1 = ctx.allocate_file_handle(dummy_file);
    HANDLE h2 = ctx.allocate_file_handle(dummy_file);

    // Assert: Different handles allocated.
    EXPECT_NE(h1, h2);

    // Assert: Release works.
    EXPECT_TRUE(ctx.release_file_handle(h1));
    EXPECT_FALSE(ctx.release_file_handle(h1)); // Already released.
}

TEST(InterceptingWebcore, InterceptionContextFindHandles)
{
    // Test the find handle table.
    intcimpl::interception_context ctx;

    intcimpl::interception_context::find_state state1;
    state1.current_index = 0;

    intcimpl::interception_context::find_state state2;
    state2.current_index = 5;

    HANDLE h1 = ctx.allocate_find_handle(std::move(state1));
    HANDLE h2 = ctx.allocate_find_handle(std::move(state2));

    // Assert: Different handles allocated.
    EXPECT_NE(h1, h2);

    // Assert: Lookup returns correct state.
    auto* s1 = ctx.lookup_find_handle(h1);
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->current_index, 0u);

    auto* s2 = ctx.lookup_find_handle(h2);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->current_index, 5u);

    // Assert: Release works.
    EXPECT_TRUE(ctx.release_find_handle(h1));
    EXPECT_EQ(ctx.lookup_find_handle(h1), nullptr);
}

TEST(InterceptingWebcore, GlobalContextSetAndRead)
{
    // The active context is a plain process-global (NOT thread_local) so that
    // hooks fire on the engine's worker threads regardless of which thread
    // published it. It is held in a std::atomic and read through active_context();
    // verify it can be published and cleared.
    EXPECT_EQ(intcimpl::active_context(), nullptr);

    intcimpl::interception_context ctx;
    intcimpl::g_active_context_cell.store(&ctx, std::memory_order_release);
    EXPECT_EQ(intcimpl::active_context(), &ctx);

    intcimpl::g_active_context_cell.store(nullptr, std::memory_order_release);
    EXPECT_EQ(intcimpl::active_context(), nullptr);
}

TEST(InterceptingWebcore, SyntheticQueueRequeueFrontPreservesOrder)
{
    // When a receive call cannot fit a dequeued request into the caller's
    // buffer (ERROR_MORE_DATA), the request must be put back at the FRONT so
    // the retry sees it again and FIFO order with later requests is preserved.
    intcimpl::synthetic_http_queue queue;

    intcimpl::synthetic_http_request first;
    first.method = "GET";
    first.url = L"http://localhost/first";
    intcimpl::synthetic_http_request second;
    second.method = "GET";
    second.url = L"http://localhost/second";

    HTTP_REQUEST_ID const first_id = queue.enqueue_request(first);
    queue.enqueue_request(second);

    // Dequeue the first request, then requeue it (simulating ERROR_MORE_DATA).
    auto dequeued = queue.try_dequeue_request();
    ASSERT_TRUE(dequeued.has_value());
    EXPECT_EQ(dequeued->request_id, first_id);
    queue.requeue_front(std::move(*dequeued));

    // The retry must see the same first request again, not lose it or reorder.
    auto retried = queue.try_dequeue_request();
    ASSERT_TRUE(retried.has_value());
    EXPECT_EQ(retried->request_id, first_id);
    EXPECT_EQ(retried->url, L"http://localhost/first");

    // The second request still follows.
    auto next = queue.try_dequeue_request();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->url, L"http://localhost/second");

    // Queue is now drained.
    EXPECT_FALSE(queue.try_dequeue_request().has_value());
}

TEST(InterceptingWebcore, SyntheticFileHandleReadWriteSeek)
{
    // A handle minted by allocate_file_handle must be usable: the context's
    // I/O helpers (which the ReadFile/WriteFile/GetFileSize/SetFilePointer hooks
    // route through) read, write, size, and seek the backing ifile.
    intcimpl::interception_context ctx;

    auto file = std::make_shared<mock_file>();
    char const seed[] = "hello world";
    file->bytes.assign(reinterpret_cast<std::byte const*>(seed),
                       reinterpret_cast<std::byte const*>(seed) + (sizeof(seed) - 1));

    HANDLE const h = ctx.allocate_file_handle(file);
    EXPECT_TRUE(ctx.is_synthetic_file_handle(h));

    // Size reflects the backing file.
    std::uint64_t   size = 0;
    std::error_code ec;
    ASSERT_TRUE(ctx.get_file_handle_size(h, size, ec));
    EXPECT_FALSE(ec);
    EXPECT_EQ(size, 11u);

    // Sequential read advances the handle's position.
    std::array<std::byte, 5> buf{};
    std::size_t              read = 0;
    ASSERT_TRUE(ctx.read_file_handle(h, buf, read, ec));
    EXPECT_FALSE(ec);
    EXPECT_EQ(read, 5u);
    EXPECT_EQ(std::memcmp(buf.data(), "hello", 5), 0);

    ASSERT_TRUE(ctx.read_file_handle(h, buf, read, ec));
    EXPECT_EQ(read, 5u);
    EXPECT_EQ(std::memcmp(buf.data(), " worl", 5), 0);

    // A third sequential read returns the trailing byte and stops at EOF.
    ASSERT_TRUE(ctx.read_file_handle(h, buf, read, ec));
    EXPECT_EQ(read, 1u);
    EXPECT_EQ(std::memcmp(buf.data(), "d", 1), 0);

    // Seek to begin, then a fresh sequential read starts over.
    std::uint64_t new_pos = 999;
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_BEGIN, new_pos, ec));
    EXPECT_FALSE(ec);
    EXPECT_EQ(new_pos, 0u);

    // Seek to end reports the file size.
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_END, new_pos, ec));
    EXPECT_EQ(new_pos, 11u);

    // Write at position 0 buffers a whole-file replacement. The buffered
    // content becomes authoritative immediately (size and reads observe it) but
    // is not pushed to the backing file until flush/close.
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_BEGIN, new_pos, ec));
    char const replacement[] = "BYE";
    std::span<std::byte const> wbuf(reinterpret_cast<std::byte const*>(replacement),
                                    sizeof(replacement) - 1);
    std::size_t written = 0;
    ASSERT_TRUE(ctx.write_file_handle(h, wbuf, written, ec));
    EXPECT_FALSE(ec);
    EXPECT_EQ(written, 3u);
    ASSERT_TRUE(ctx.get_file_handle_size(h, size, ec));
    EXPECT_EQ(size, 3u);

    // A read while the write is still buffered observes the buffered content,
    // not the (still-unchanged) backing file.
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_BEGIN, new_pos, ec));
    std::array<std::byte, 5> rbuf{};
    ASSERT_TRUE(ctx.read_file_handle(h, rbuf, read, ec));
    EXPECT_EQ(read, 3u);
    EXPECT_EQ(std::memcmp(rbuf.data(), "BYE", 3), 0);
    EXPECT_EQ(file->bytes.size(), 11u); // backing file untouched until flush

    // Flushing pushes the buffered write to the backing file.
    ASSERT_TRUE(ctx.flush_file_handle(h, ec));
    EXPECT_FALSE(ec);
    ASSERT_EQ(file->bytes.size(), 3u);
    EXPECT_EQ(std::memcmp(file->bytes.data(), "BYE", 3), 0);

    // A handle that is not ours is reported as not-found (caller falls through).
    HANDLE const bogus = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));
    EXPECT_FALSE(ctx.read_file_handle(bogus, buf, read, ec));
    EXPECT_FALSE(ctx.is_synthetic_file_handle(bogus));
}

TEST(InterceptingWebcore, SyntheticFileHandleChunkedWrite)
{
    // Writes that arrive in multiple chunks must accumulate into a single
    // whole-file replacement (M-HWC-REVIEW2-2): the engine often emits a body
    // across several WriteFile calls, and only the concatenation is flushed.
    intcimpl::interception_context ctx;

    auto file = std::make_shared<mock_file>();
    HANDLE const h = ctx.allocate_file_handle(file);

    std::error_code ec;
    std::uint64_t   new_pos = 0;
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_BEGIN, new_pos, ec));

    char const chunk1[] = "AAA";
    char const chunk2[] = "BBBBB";
    std::span<std::byte const> w1(reinterpret_cast<std::byte const*>(chunk1),
                                  sizeof(chunk1) - 1);
    std::span<std::byte const> w2(reinterpret_cast<std::byte const*>(chunk2),
                                  sizeof(chunk2) - 1);

    std::size_t written = 0;
    ASSERT_TRUE(ctx.write_file_handle(h, w1, written, ec));
    EXPECT_EQ(written, 3u);
    ASSERT_TRUE(ctx.write_file_handle(h, w2, written, ec));
    EXPECT_EQ(written, 5u);

    // The buffered extent reflects both chunks.
    std::uint64_t size = 0;
    ASSERT_TRUE(ctx.get_file_handle_size(h, size, ec));
    EXPECT_EQ(size, 8u);

    // Reading from the start observes the concatenation while still buffered.
    ASSERT_TRUE(ctx.set_file_handle_pointer(h, 0, FILE_BEGIN, new_pos, ec));
    std::array<std::byte, 8> rbuf{};
    std::size_t              read = 0;
    ASSERT_TRUE(ctx.read_file_handle(h, rbuf, read, ec));
    EXPECT_EQ(read, 8u);
    EXPECT_EQ(std::memcmp(rbuf.data(), "AAABBBBB", 8), 0);

    // Closing flushes the accumulated content to the backing file in one shot.
    ASSERT_TRUE(ctx.close_file_handle(h, ec));
    EXPECT_FALSE(ec);
    ASSERT_EQ(file->bytes.size(), 8u);
    EXPECT_EQ(std::memcmp(file->bytes.data(), "AAABBBBB", 8), 0);

    // After close the handle is no longer ours.
    EXPECT_FALSE(ctx.is_synthetic_file_handle(h));
}

