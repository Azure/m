// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/webcore_interfaces.h>

//
// Materializing webcore decorator (D-HWC-4, M-HWC-MATERIALIZE).
//
// This decorator wraps an underlying `iwebcore` provider and interposes on
// `activate` to materialize the engine's config and content from the isolated
// filesystem into a real temp directory before calling the underlying provider.
//
// On activate:
//   1. Read the `applicationHost.config` from the isolated filesystem.
//   2. Parse it to find all `physicalPath` attributes (content roots).
//   3. Create a per-instance temp directory.
//   4. Project every referenced content root from the isolated FS into the
//      temp directory (copy the directory tree).
//   5. Rewrite the config's `physicalPath` values to point to the materialized
//      locations.
//   6. Write the rewritten config to the temp directory.
//   7. Call the underlying `iwebcore::activate` with the temp config path.
//
// On instance destruction:
//   1. Shut down the underlying instance.
//   2. Delete the temp directory (the materialized projection).
//
// This is the documented **isolation boundary** (D-HWC-4): at the moment
// control passes to un-shimmed native code, isolation becomes concrete.
//

namespace m::pil::impl::materializing
{
    //--------------------------------------------------------------------------
    // Path mapping entry: original (isolated) path → materialized (real) path
    //--------------------------------------------------------------------------

    struct path_mapping
    {
        file_path   original_path;      // path in the isolated filesystem
        std::filesystem::path materialized_path; // path in the temp directory
    };

    //--------------------------------------------------------------------------
    // materializing_webcore_instance — RAII token with cleanup
    //--------------------------------------------------------------------------

    class webcore_instance final : public iwebcore_instance
    {
    public:
        webcore_instance() = delete;
        webcore_instance(webcore_instance const&) = delete;
        webcore_instance(webcore_instance&&) = delete;
        webcore_instance& operator=(webcore_instance const&) = delete;
        webcore_instance& operator=(webcore_instance&&) = delete;

        // Constructs the RAII token. Takes ownership of the underlying instance
        // and the temp directory path for cleanup.
        webcore_instance(std::unique_ptr<iwebcore_instance> underlying_instance,
                         std::filesystem::path              temp_dir);

        ~webcore_instance() override;

    private:
        std::unique_ptr<iwebcore_instance> m_underlying_instance;
        std::filesystem::path              m_temp_dir;
    };

    //--------------------------------------------------------------------------
    // materializing_webcore — decorator that materializes config/content
    //--------------------------------------------------------------------------

    class webcore final : public iwebcore, public std::enable_shared_from_this<webcore>
    {
    public:
        // Construct with references to the isolated filesystem and the
        // underlying (direct) webcore provider.
        webcore(std::shared_ptr<ifilesystem> isolated_filesystem,
                std::shared_ptr<iwebcore>    underlying_webcore);

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
        // Create a unique temp directory for this activation.
        std::filesystem::path
        create_temp_directory(std::error_code& ec);

        // Read file content from the isolated filesystem.
        std::vector<std::byte>
        read_isolated_file(file_path const& path, std::error_code& ec);

        // Parse applicationHost.config and extract all physicalPath values.
        std::vector<file_path>
        extract_physical_paths(std::vector<std::byte> const& config_content,
                               std::error_code&              ec);

        // Project (copy) a directory tree from the isolated FS to a real path.
        void
        project_directory(file_path const&              source_path,
                          std::filesystem::path const&  dest_path,
                          std::error_code&              ec);

        // Rewrite physicalPath values in the config and return the new content.
        std::vector<std::byte>
        rewrite_config(std::vector<std::byte> const&    original_content,
                       std::vector<path_mapping> const& mappings,
                       std::error_code&                 ec);

        // Write bytes to a real filesystem path.
        void
        write_real_file(std::filesystem::path const&  path,
                        std::vector<std::byte> const& content,
                        std::error_code&              ec);

        std::mutex                   m_mutex;
        std::shared_ptr<ifilesystem> m_isolated_filesystem;
        std::shared_ptr<iwebcore>    m_underlying_webcore;
    };

    //--------------------------------------------------------------------------
    // Factory function
    //--------------------------------------------------------------------------

    std::shared_ptr<iwebcore>
    create_materializing_webcore(std::shared_ptr<ifilesystem> isolated_filesystem,
                                 std::shared_ptr<iwebcore>    underlying_webcore);

} // namespace m::pil::impl::materializing
