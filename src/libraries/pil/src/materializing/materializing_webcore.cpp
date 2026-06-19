// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "materializing_webcore.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/strings/convert.h>

#include "../pugihelp.h"
#include <pugixml.hpp>

// Windows headers for temp path
#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>

namespace
{
    // Generate a unique directory name based on timestamp + random suffix.
    std::wstring
    generate_unique_dir_name()
    {
        auto const now = std::chrono::system_clock::now();
        auto const epoch_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::random_device         rd;
        std::mt19937               gen(rd());
        std::uniform_int_distribution<uint32_t> distrib(0, 0xFFFFFF);
        auto const                 suffix = distrib(gen);

        std::wostringstream oss;
        oss << L"pil_hwc_" << epoch_ms << L"_" << std::hex << suffix;
        return oss.str();
    }

    // Convert file_path (char16_t*) to std::filesystem::path (wchar_t*).
    std::filesystem::path
    file_path_to_fs_path(m::pil::file_path const& fp)
    {
        // On Windows, char16_t and wchar_t are both 16-bit UTF-16 code units.
        return std::filesystem::path(
            reinterpret_cast<wchar_t const*>(fp.c_str()));
    }

    // Convert std::filesystem::path to file_path.
    m::pil::file_path
    fs_path_to_file_path(std::filesystem::path const& p)
    {
        std::wstring const ws = p.wstring();
        return m::pil::file_path(
            std::u16string_view(reinterpret_cast<char16_t const*>(ws.data()), ws.size()));
    }

    // Get the Windows temp directory.
    std::filesystem::path
    get_temp_directory()
    {
        wchar_t temp_path[MAX_PATH + 1] = {};
        auto const len = ::GetTempPathW(static_cast<DWORD>(std::size(temp_path)), temp_path);
        if (len == 0 || len >= std::size(temp_path))
        {
            // Fall back to current directory.
            return std::filesystem::current_path();
        }
        return std::filesystem::path(temp_path, temp_path + len);
    }

} // namespace

namespace m::pil::impl::materializing
{
    //--------------------------------------------------------------------------
    // webcore_instance
    //--------------------------------------------------------------------------

    webcore_instance::webcore_instance(std::unique_ptr<iwebcore_instance> underlying_instance,
                                       std::filesystem::path              temp_dir):
        m_underlying_instance(std::move(underlying_instance)),
        m_temp_dir(std::move(temp_dir))
    {
        M_INTERNAL_ERROR_CHECK(m_underlying_instance != nullptr);
    }

    webcore_instance::~webcore_instance()
    {
        // First, shut down the underlying instance.
        m_underlying_instance.reset();

        // Then, clean up the temp directory.
        if (!m_temp_dir.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(m_temp_dir, ec);
            // Ignore errors during cleanup — destructor cannot throw.
        }
    }

    //--------------------------------------------------------------------------
    // webcore
    //--------------------------------------------------------------------------

    webcore::webcore(std::shared_ptr<ifilesystem> isolated_filesystem,
                     std::shared_ptr<iwebcore>    underlying_webcore):
        m_isolated_filesystem(std::move(isolated_filesystem)),
        m_underlying_webcore(std::move(underlying_webcore))
    {
        M_INTERNAL_ERROR_CHECK(m_isolated_filesystem != nullptr);
        M_INTERNAL_ERROR_CHECK(m_underlying_webcore != nullptr);
    }

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        ec.clear();
        returned_instance.reset();

        std::lock_guard<std::mutex> guard(m_mutex);

        // Step 1: Create a unique temp directory for this activation.
        auto const temp_dir = create_temp_directory(ec);
        if (ec)
            return {};

        // Step 2: Read the applicationHost.config from the isolated filesystem.
        auto const config_content = read_isolated_file(request.app_host_config, ec);
        if (ec)
        {
            // Clean up temp dir on failure.
            std::error_code ignore_ec;
            std::filesystem::remove_all(temp_dir, ignore_ec);
            return {};
        }

        // Step 3: Parse the config and extract all physicalPath values.
        auto const physical_paths = extract_physical_paths(config_content, ec);
        if (ec)
        {
            std::error_code ignore_ec;
            std::filesystem::remove_all(temp_dir, ignore_ec);
            return {};
        }

        // Step 4: Build the path mappings and project content.
        std::vector<path_mapping> mappings;
        mappings.reserve(physical_paths.size());

        for (auto const& original_path : physical_paths)
        {
            // Create a unique subdirectory name for this content root.
            // Use a hash-like encoding of the original path to keep names unique.
            std::wstring const original_wstr =
                reinterpret_cast<wchar_t const*>(original_path.c_str());

            // Simple approach: replace path separators with underscores and
            // remove the drive letter to create a flat name.
            std::wstring flat_name;
            flat_name.reserve(original_wstr.size());
            for (wchar_t ch : original_wstr)
            {
                if (ch == L'\\' || ch == L'/' || ch == L':')
                    flat_name.push_back(L'_');
                else
                    flat_name.push_back(ch);
            }
            // Trim leading underscores.
            while (!flat_name.empty() && flat_name.front() == L'_')
                flat_name.erase(flat_name.begin());

            auto const content_dir = temp_dir / L"content" / flat_name;

            // Project the directory from isolated FS to the real temp location.
            project_directory(original_path, content_dir, ec);
            if (ec)
            {
                std::error_code ignore_ec;
                std::filesystem::remove_all(temp_dir, ignore_ec);
                return {};
            }

            mappings.push_back(path_mapping{original_path, content_dir});
        }

        // Step 5: Rewrite the config with the materialized paths.
        auto const rewritten_content = rewrite_config(config_content, mappings, ec);
        if (ec)
        {
            std::error_code ignore_ec;
            std::filesystem::remove_all(temp_dir, ignore_ec);
            return {};
        }

        // Step 6: Write the rewritten config to the temp directory.
        auto const materialized_config_path = temp_dir / L"applicationHost.config";
        write_real_file(materialized_config_path, rewritten_content, ec);
        if (ec)
        {
            std::error_code ignore_ec;
            std::filesystem::remove_all(temp_dir, ignore_ec);
            return {};
        }

        // Step 7: Build a new activation request with the materialized config path.
        activation_request materialized_request;
        materialized_request.app_host_config = fs_path_to_file_path(materialized_config_path);

        // Also materialize root_web_config if present.
        if (request.root_web_config)
        {
            auto const root_config_content =
                read_isolated_file(*request.root_web_config, ec);
            if (ec)
            {
                std::error_code ignore_ec;
                std::filesystem::remove_all(temp_dir, ignore_ec);
                return {};
            }

            // root web.config may also reference paths, but for now we just copy it as-is.
            // A more complete implementation would also rewrite this file.
            auto const materialized_root_path = temp_dir / L"web.config";
            write_real_file(materialized_root_path, root_config_content, ec);
            if (ec)
            {
                std::error_code ignore_ec;
                std::filesystem::remove_all(temp_dir, ignore_ec);
                return {};
            }

            materialized_request.root_web_config = fs_path_to_file_path(materialized_root_path);
        }

        materialized_request.instance_name = request.instance_name;

        // Step 8: Call the underlying webcore with the materialized request.
        std::unique_ptr<iwebcore_instance> underlying_instance;
        auto const d = m_underlying_webcore->activate(flags, materialized_request, underlying_instance, ec);

        if (ec || !underlying_instance)
        {
            std::error_code ignore_ec;
            std::filesystem::remove_all(temp_dir, ignore_ec);
            return d;
        }

        // Step 9: Wrap the underlying instance in a materializing instance
        // that will clean up the temp directory on destruction.
        returned_instance =
            std::make_unique<webcore_instance>(std::move(underlying_instance), temp_dir);

        return d;
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        // set_metadata is a pass-through — no materialization needed.
        return m_underlying_webcore->set_metadata(flags, type, value, ec);
    }

    std::filesystem::path
    webcore::create_temp_directory(std::error_code& ec)
    {
        ec.clear();

        auto const base_dir = get_temp_directory();
        auto const unique_name = generate_unique_dir_name();
        auto const temp_dir = base_dir / unique_name;

        std::filesystem::create_directories(temp_dir, ec);
        if (ec)
            return {};

        return temp_dir;
    }

    std::vector<std::byte>
    webcore::read_isolated_file(file_path const& path, std::error_code& ec)
    {
        ec.clear();

        // Parse the path to extract root and relative path.
        auto const root = path.root();
        auto const rel  = path.relative_path();

        // Open the root directory from the isolated filesystem.
        std::shared_ptr<idirectory> root_dir;
        auto const root_d = m_isolated_filesystem->open_root(
            ifilesystem::open_root_flags{}, root, file_access::default_open, root_dir);
        (void)root_d;
        if (!root_dir)
        {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return {};
        }

        // Open the file from the root directory.
        std::shared_ptr<ifile> file;
        auto const open_d = root_dir->open_file(
            idirectory::open_file_flags{}, file_path(rel), file_access::default_open, file, ec);
        if (ec)
            return {};
        if (!file)
        {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return {};
        }

        // Get the file size.
        file_metadata metadata;
        auto const query_d = file->query_information(ifile::query_information_flags{}, metadata);
        (void)query_d;

        auto const file_size = metadata.m_size;
        if (file_size == 0)
            return {};

        // Read the content.
        std::vector<std::byte> buffer(static_cast<std::size_t>(file_size));
        std::size_t            bytes_read = 0;
        auto const read_d = file->read_content(
            ifile::read_content_flags{}, 0, std::span<std::byte>(buffer), bytes_read, ec);
        if (ec)
            return {};

        buffer.resize(bytes_read);
        return buffer;
    }

    std::vector<file_path>
    webcore::extract_physical_paths(std::vector<std::byte> const& config_content,
                                    std::error_code&              ec)
    {
        ec.clear();

        std::vector<file_path> result;

        if (config_content.empty())
            return result;

        // Parse as XML. pugixml expects a null-terminated string.
        pugi::xml_document doc;

        // Config is typically UTF-8 or UTF-16. Try to load it.
        auto const load_result = doc.load_buffer(
            config_content.data(),
            config_content.size(),
            pugi::parse_default,
            pugi::encoding_auto);

        if (!load_result)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return result;
        }

        // Find all physicalPath attributes in the document.
        // The typical structure is:
        //   <configuration>
        //     <system.applicationHost>
        //       <sites>
        //         <site ...>
        //           <application path="...">
        //             <virtualDirectory path="..." physicalPath="C:\..." />
        //           </application>
        //         </site>
        //       </sites>
        //     </system.applicationHost>
        //   </configuration>
        //
        // We need to find all physicalPath attributes anywhere in the document.

        for (auto const& node : doc.select_nodes(M_PUGIXML_T("//*[@physicalPath]")))
        {
            auto const attr = node.node().attribute(M_PUGIXML_T("physicalPath"));
            if (attr)
            {
                // attr.value() is pugi::char_t* which is wchar_t* in WCHAR mode.
                auto const* const value = attr.value();
                if (value && value[0] != L'\0')
                {
                    // Convert wchar_t* to file_path (char16_t*).
                    std::u16string_view u16_value(
                        reinterpret_cast<char16_t const*>(value),
                        std::wcslen(value));
                    file_path fp(u16_value);

                    // Avoid duplicates.
                    bool found = false;
                    for (auto const& existing : result)
                    {
                        if (existing == fp)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        result.push_back(std::move(fp));
                }
            }
        }

        return result;
    }

    void
    webcore::project_directory(file_path const&             source_path,
                               std::filesystem::path const& dest_path,
                               std::error_code&             ec)
    {
        ec.clear();

        // Create the destination directory.
        std::filesystem::create_directories(dest_path, ec);
        if (ec)
            return;

        // Parse the source path to get root and relative.
        auto const root = source_path.root();
        auto const rel  = source_path.relative_path();

        // Open the root directory from the isolated filesystem.
        std::shared_ptr<idirectory> root_dir;
        auto const root_d = m_isolated_filesystem->open_root(
            ifilesystem::open_root_flags{}, root, file_access::default_open, root_dir);
        (void)root_d;
        if (!root_dir)
        {
            // Root doesn't exist — no content to project.
            return;
        }

        // Open the source directory from the root.
        std::shared_ptr<idirectory> source_dir;
        auto const open_d = root_dir->open_directory(
            idirectory::open_directory_flags::tolerate_not_found,
            file_path(rel),
            file_access::default_open,
            source_dir,
            ec);
        if (ec)
            return;

        if (!source_dir)
        {
            // Directory doesn't exist in isolated FS — this is allowed, the
            // engine may reference paths that don't exist in the isolated view.
            // Create an empty directory.
            return;
        }

        // Enumerate and recursively copy entries.
        std::size_t index = 0;
        while (true)
        {
            auto const entry_opt = source_dir->enumerate_entries(index);
            if (!entry_opt)
                break;

            auto const& entry = *entry_opt;
            auto const  entry_name_view = entry.m_name.view();
            auto const  entry_name = std::filesystem::path(
                reinterpret_cast<wchar_t const*>(entry_name_view.data()),
                reinterpret_cast<wchar_t const*>(entry_name_view.data() + entry_name_view.size()));
            auto const child_dest_path = dest_path / entry_name;

            // Build the child source path by appending the entry name.
            auto const child_source_path = source_path / file_path(entry_name_view);

            if (entry.m_kind == node_kind::directory)
            {
                // Recurse into subdirectory.
                project_directory(child_source_path, child_dest_path, ec);
                if (ec)
                    return;
            }
            else
            {
                // Copy file content.
                auto const file_content = read_isolated_file(child_source_path, ec);
                if (ec)
                    return;

                write_real_file(child_dest_path, file_content, ec);
                if (ec)
                    return;
            }

            ++index;
        }
    }

    std::vector<std::byte>
    webcore::rewrite_config(std::vector<std::byte> const&    original_content,
                            std::vector<path_mapping> const& mappings,
                            std::error_code&                 ec)
    {
        ec.clear();

        if (original_content.empty() || mappings.empty())
            return original_content;

        // Parse the config again.
        pugi::xml_document doc;
        auto const load_result = doc.load_buffer(
            original_content.data(),
            original_content.size(),
            pugi::parse_default,
            pugi::encoding_auto);

        if (!load_result)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return {};
        }

        // Replace physicalPath values.
        for (auto const& mapping : mappings)
        {
            std::wstring const original_wstr =
                reinterpret_cast<wchar_t const*>(mapping.original_path.c_str());
            std::wstring const materialized_wstr = mapping.materialized_path.wstring();

            for (auto const& node : doc.select_nodes(M_PUGIXML_T("//*[@physicalPath]")))
            {
                auto attr = node.node().attribute(M_PUGIXML_T("physicalPath"));
                if (attr)
                {
                    std::wstring current_value = attr.value();
                    // Case-insensitive comparison (Windows paths are case-insensitive).
                    if (_wcsicmp(current_value.c_str(), original_wstr.c_str()) == 0)
                    {
                        attr.set_value(materialized_wstr.c_str());
                    }
                }
            }
        }

        // Serialize the modified document to a UTF-8 string.
        std::ostringstream oss;
        doc.save(oss, M_PUGIXML_T("  "), pugi::format_default, pugi::encoding_utf8);
        std::string const utf8_str = oss.str();

        std::vector<std::byte> result(utf8_str.size());
        std::memcpy(result.data(), utf8_str.data(), utf8_str.size());

        return result;
    }

    void
    webcore::write_real_file(std::filesystem::path const&  path,
                             std::vector<std::byte> const& content,
                             std::error_code&              ec)
    {
        ec.clear();

        // Ensure parent directory exists.
        auto const parent = path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
            if (ec)
                return;
        }

        // Write the content using standard C++ file I/O.
        std::ofstream ofs(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!ofs)
        {
            ec = std::make_error_code(std::errc::io_error);
            return;
        }

        if (!content.empty())
        {
            ofs.write(reinterpret_cast<char const*>(content.data()),
                      static_cast<std::streamsize>(content.size()));
            if (!ofs)
            {
                ec = std::make_error_code(std::errc::io_error);
                return;
            }
        }

        ofs.close();
    }

    //--------------------------------------------------------------------------
    // Factory function
    //--------------------------------------------------------------------------

    std::shared_ptr<iwebcore>
    create_materializing_webcore(std::shared_ptr<ifilesystem> isolated_filesystem,
                                 std::shared_ptr<iwebcore>    underlying_webcore)
    {
        return std::make_shared<webcore>(std::move(isolated_filesystem),
                                         std::move(underlying_webcore));
    }

} // namespace m::pil::impl::materializing
