// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// M-HWC-MATERIALIZE-2: Integration test for the materializing webcore decorator.
// Validates that:
//   1. Config is read from the isolated (buffered) filesystem
//   2. physicalPath attributes are extracted and projected to a real temp dir
//   3. The config is rewritten with the materialized paths
//   4. The underlying webcore receives real paths that exist on disk
//

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/webcore_interfaces.h>

#include "buffered/buffered.h"
#include "materializing/materializing_webcore.h"

#include <pugixml.hpp>

namespace
{
    namespace bufimpl = m::pil::impl::buffered;
    namespace matimpl = m::pil::impl::materializing;

    //--------------------------------------------------------------------------
    // Mock webcore — records the paths received at activation
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
    };

    //--------------------------------------------------------------------------
    // Helper functions
    //--------------------------------------------------------------------------

    m::pil::file_path
    to_file_path(std::filesystem::path const& p)
    {
        std::wstring const   ws = p.wstring();
        std::u16string const u16(ws.begin(), ws.end());
        return m::pil::file_path(m::pil::file_path::view_type(u16));
    }

    std::wstring
    to_wstring(m::pil::file_path const& fp)
    {
        return std::wstring(reinterpret_cast<wchar_t const*>(fp.c_str()));
    }

    // Reads a file's contents into a string.
    std::string
    read_file_content(std::filesystem::path const& path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
            return {};
        return std::string((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
    }

    // Scoped temp directory with auto-cleanup.
    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_mat_wc_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
                             std::to_wstring(s_counter++));
            std::filesystem::remove_all(m_path);
            std::filesystem::create_directories(m_path);
        }

        scoped_temp_dir(scoped_temp_dir const&)            = delete;
        scoped_temp_dir& operator=(scoped_temp_dir const&) = delete;

        ~scoped_temp_dir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }

        std::filesystem::path const&
        path() const noexcept
        {
            return m_path;
        }

    private:
        static inline unsigned s_counter = 0;
        std::filesystem::path  m_path;
    };

    // Creates a buffered filesystem over the platform's live filesystem.
    std::shared_ptr<bufimpl::filesystem>
    make_buffered_filesystem()
    {
        auto platform = m::pil::make_platform_interface();
        std::shared_ptr<m::pil::ifilesystem> underlying_fs;
        platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, underlying_fs);
        return std::make_shared<bufimpl::filesystem>(underlying_fs);
    }

    // Opens a directory in the buffered filesystem, triggering capture.
    void
    capture_directory(m::pil::ifilesystem& fs, std::filesystem::path const& path)
    {
        auto const fp = to_file_path(path);
        auto root = fs.open_root(fp.root(), m::pil::file_access::default_open);
        (void)root->open_directory(m::pil::file_path(fp.relative_path()));
    }

} // namespace

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

TEST(MaterializingWebcore, BasicActivationWithBufferedFilesystem)
{
    // Arrange: Create a temp directory with a minimal IIS configuration.
    scoped_temp_dir temp;

    // Create a minimal applicationHost.config with a physicalPath attribute.
    std::string const config_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<configuration>
  <system.applicationHost>
    <sites>
      <site name="Default Web Site" id="1">
        <application path="/">
          <virtualDirectory path="/" physicalPath=")" +
        temp.path().string() + R"(\wwwroot" />
        </application>
      </site>
    </sites>
  </system.applicationHost>
</configuration>
)";

    auto const config_path = temp.path() / L"applicationHost.config";
    {
        std::ofstream ofs(config_path, std::ios::binary);
        ofs.write(config_content.data(), static_cast<std::streamsize>(config_content.size()));
    }

    // Create the content root with a sample file.
    auto const wwwroot_path = temp.path() / L"wwwroot";
    std::filesystem::create_directories(wwwroot_path);
    {
        std::ofstream ofs(wwwroot_path / L"index.html", std::ios::binary);
        std::string const html = "<html><body>Hello</body></html>";
        ofs.write(html.data(), static_cast<std::streamsize>(html.size()));
    }

    // Create buffered filesystem and capture the temp directory structure.
    auto buffered_fs = make_buffered_filesystem();
    capture_directory(*buffered_fs, temp.path());
    capture_directory(*buffered_fs, wwwroot_path);

    // Create the mock underlying webcore.
    auto mock_underlying = std::make_shared<mock_webcore>();

    // Create the materializing webcore.
    auto materializing_wc = matimpl::create_materializing_webcore(buffered_fs, mock_underlying);

    // Act: Activate the webcore.
    m::pil::activation_request request;
    request.app_host_config = to_file_path(config_path);
    request.instance_name   = u"TestInstance";

    std::unique_ptr<m::pil::iwebcore_instance> instance;
    std::error_code                            ec;
    auto const d = materializing_wc->activate(
        m::pil::iwebcore::activate_flags{}, request, instance, ec);

    // Assert: Activation succeeded.
    ASSERT_FALSE(ec) << "Activation failed: " << ec.message();
    ASSERT_TRUE(instance);

    // Assert: The mock received exactly one activation.
    ASSERT_EQ(1u, mock_underlying->activations().size());

    auto const& recorded = mock_underlying->activations()[0];

    // Assert: The config path received by the mock is a real path (not the original).
    auto const received_config_wstr = to_wstring(recorded.app_host_config);
    ASSERT_FALSE(received_config_wstr.empty());
    EXPECT_NE(received_config_wstr, config_path.wstring())
        << "Mock received original path instead of materialized path";

    // Assert: The materialized config file exists.
    std::filesystem::path const received_config_fspath(received_config_wstr);
    EXPECT_TRUE(std::filesystem::exists(received_config_fspath))
        << "Materialized config does not exist: " << received_config_fspath;

    // Assert: The materialized config contains rewritten physicalPath values.
    std::string const materialized_content = read_file_content(received_config_fspath);
    ASSERT_FALSE(materialized_content.empty());

    // The physicalPath should NOT reference the original temp path.
    EXPECT_EQ(materialized_content.find(temp.path().string()), std::string::npos)
        << "Materialized config still references original path";

    // Assert: Instance name was forwarded.
    EXPECT_EQ(recorded.instance_name, u"TestInstance");

    // Cleanup: Destroy the instance (which should clean up the temp projection).
    instance.reset();

    // Assert: After instance destruction, the materialized config should be deleted.
    // Give a brief moment for cleanup (the destructor is synchronous, but just in case).
    EXPECT_FALSE(std::filesystem::exists(received_config_fspath))
        << "Materialized config was not cleaned up after instance destruction";
}

TEST(MaterializingWebcore, ProjectsContentToTempDirectory)
{
    // Arrange: Create a temp directory with configuration and content.
    scoped_temp_dir temp;

    // Create a content root with a nested structure.
    auto const wwwroot_path = temp.path() / L"wwwroot";
    std::filesystem::create_directories(wwwroot_path / L"css");
    std::filesystem::create_directories(wwwroot_path / L"js");

    {
        std::ofstream ofs(wwwroot_path / L"index.html");
        ofs << "<html><body>Index</body></html>";
    }
    {
        std::ofstream ofs(wwwroot_path / L"css" / L"style.css");
        ofs << "body { color: black; }";
    }
    {
        std::ofstream ofs(wwwroot_path / L"js" / L"app.js");
        ofs << "console.log('hello');";
    }

    // Create applicationHost.config.
    std::string const config_content = R"(<?xml version="1.0" encoding="UTF-8"?>
<configuration>
  <system.applicationHost>
    <sites>
      <site name="Default" id="1">
        <application path="/">
          <virtualDirectory path="/" physicalPath=")" +
        wwwroot_path.string() + R"(" />
        </application>
      </site>
    </sites>
  </system.applicationHost>
</configuration>
)";

    auto const config_path = temp.path() / L"applicationHost.config";
    {
        std::ofstream ofs(config_path, std::ios::binary);
        ofs << config_content;
    }

    // Create buffered filesystem and capture the directory structure.
    auto buffered_fs = make_buffered_filesystem();
    capture_directory(*buffered_fs, temp.path());
    capture_directory(*buffered_fs, wwwroot_path);
    capture_directory(*buffered_fs, wwwroot_path / L"css");
    capture_directory(*buffered_fs, wwwroot_path / L"js");

    // Create mock webcore and materializing webcore.
    auto mock_underlying = std::make_shared<mock_webcore>();
    auto materializing_wc = matimpl::create_materializing_webcore(buffered_fs, mock_underlying);

    // Act: Activate.
    m::pil::activation_request request;
    request.app_host_config = to_file_path(config_path);
    request.instance_name   = u"ContentTest";

    std::unique_ptr<m::pil::iwebcore_instance> instance;
    std::error_code                            ec;
    materializing_wc->activate(m::pil::iwebcore::activate_flags{}, request, instance, ec);

    ASSERT_FALSE(ec);
    ASSERT_TRUE(instance);

    // Assert: The mock received the activation.
    ASSERT_EQ(1u, mock_underlying->activations().size());

    // Read the materialized config to find the projected content path.
    auto const& recorded = mock_underlying->activations()[0];
    auto const  received_config_wstr = to_wstring(recorded.app_host_config);
    std::filesystem::path const received_config_fspath(received_config_wstr);

    std::string const materialized_content = read_file_content(received_config_fspath);

    // Parse the materialized config to extract the physicalPath.
    // Convert UTF-8 content to wstring for pugixml in WCHAR mode.
    std::wstring const wide_content(materialized_content.begin(), materialized_content.end());

    pugi::xml_document doc;
    doc.load_string(wide_content.c_str());

    auto const virtual_dir = doc.select_node(L"//*[@physicalPath]");
    ASSERT_TRUE(virtual_dir);

    std::wstring const projected_path = virtual_dir.node().attribute(L"physicalPath").value();
    ASSERT_FALSE(projected_path.empty());

    // Assert: The projected path is different from the original.
    EXPECT_NE(projected_path, wwwroot_path.wstring());

    // Assert: The projected content exists with the correct structure.
    std::filesystem::path const projected_fspath(projected_path);
    EXPECT_TRUE(std::filesystem::exists(projected_fspath / L"index.html"));
    EXPECT_TRUE(std::filesystem::exists(projected_fspath / L"css" / L"style.css"));
    EXPECT_TRUE(std::filesystem::exists(projected_fspath / L"js" / L"app.js"));

    // Assert: Content is correct.
    std::string const index_content = read_file_content(projected_fspath / L"index.html");
    EXPECT_NE(index_content.find("Index"), std::string::npos);

    // Cleanup.
    instance.reset();
}
