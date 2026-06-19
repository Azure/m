// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "buffered.h"

using namespace std::string_view_literals;

namespace m::pil::impl::buffered
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform),
        m_registry(std::make_shared<registry>(m_underlying_platform->get_registry())),
        m_filesystem(std::make_shared<filesystem>(m_underlying_platform->get_filesystem()))
    {}

    platform::platform(std::shared_ptr<iplatform>&& underlying_platform):
        m_underlying_platform(std::move(underlying_platform)),
        m_registry(std::make_shared<registry>(m_underlying_platform->get_registry())),
        m_filesystem(std::make_shared<filesystem>(m_underlying_platform->get_filesystem()))
    {}

    platform::platform(std::shared_ptr<registry>   snapshot_registry,
                       std::shared_ptr<filesystem> snapshot_filesystem):
        m_registry(std::move(snapshot_registry)), m_filesystem(std::move(snapshot_filesystem))
    {
        // Snapshot platform: m_underlying_platform is intentionally null so that
        // reads and writes operate purely against the loaded state.
    }

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("iplatform::get_registry() called with invalid flags");

        auto l            = std::unique_lock(m_mutex);
        returned_registry = m_registry;
        return get_registry_disposition{};
    }

    iplatform::get_filesystem_disposition
    platform::get_filesystem(get_filesystem_flags          flags,
                             std::shared_ptr<ifilesystem>& returned_filesystem)
    {
        returned_filesystem.reset();

        if (flags != get_filesystem_flags{})
            throw std::runtime_error("iplatform::get_filesystem() called with invalid flags");

        auto l              = std::unique_lock(m_mutex);
        returned_filesystem = m_filesystem;
        return get_filesystem_disposition{};
    }

    iplatform::get_webcore_disposition
    platform::get_webcore(get_webcore_flags, std::shared_ptr<iwebcore>&)
    {
        // M-HWC-FACETS-4: Buffered get_webcore returns M_NOT_IMPLEMENTED — an
        // engine is not snapshotted (D-HWC-1).
        M_NOT_IMPLEMENTED("buffered::platform::get_webcore");
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // The buffered layer owns the change-log overlay, so this is where the
        // persisted state is produced: serialize our registry and filesystem
        // overlays into the supplied <Platform> element, then let lower layers
        // (which hold no change log) contribute anything of their own.
        {
            auto l = std::unique_lock(m_mutex);
            m_registry->save_xml(platform_element);
            m_filesystem->save_xml(platform_element);
        }

        if (m_underlying_platform)
            return m_underlying_platform->save(flags, contents, platform_element);

        return save_disposition{};
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // D6: pass the diagnostic-log request through to lower layers so a
        // logging tap placed beneath the buffered layer is reachable. A
        // snapshot platform has no underlying and contributes nothing.
        if (m_underlying_platform)
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);

        return save_disposition{};
    }

    std::shared_ptr<iplatform>
    create_platform_from_persisted_xml(std::filesystem::path const& p)
    {
        pugi::xml_document doc;

        auto const result = doc.load_file(p.native().c_str());
        if (!result)
            throw std::runtime_error(
                std::string("buffered::create_platform_from_persisted_xml: failed to load ") +
                result.description());

        auto platform_node = doc.document_element();

        auto reg = registry::load_xml(platform_node);
        auto fs  = filesystem::load_xml(platform_node);

        return std::make_shared<platform>(std::move(reg), std::move(fs));
    }

} // namespace m::pil::impl::buffered
