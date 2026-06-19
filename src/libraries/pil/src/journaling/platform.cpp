// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/error_handling/macros.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>

#include "journaling.h"

using namespace std::string_view_literals;

namespace m::pil::impl::journaling
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform),
        m_journal(std::make_shared<journal>()),
        m_registry{std::make_shared<registry>(m_underlying_platform->get_registry(), m_journal)},
        m_filesystem{std::make_shared<filesystem>(m_underlying_platform->get_filesystem(), m_journal)}
    {}

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("iplatform::get_registry() called with invalid flags");

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

        returned_filesystem = m_filesystem;

        return get_filesystem_disposition{};
    }

    iplatform::get_webcore_disposition
    platform::get_webcore(get_webcore_flags, std::shared_ptr<iwebcore>&)
    {
        // M-HWC-FACETS-4: Journaling get_webcore returns M_NOT_IMPLEMENTED — an
        // engine is not snapshotted (D-HWC-1).
        M_NOT_IMPLEMENTED("journaling::platform::get_webcore");
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // D7: the journal is a separate artifact, never folded into the
        // persisted <Platform>. Persistence is a transparent pass-through.
        return m_underlying_platform->save(flags, contents, platform_element);
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // The journaling layer records no diagnostic trace of its own; forward
        // so a logging tap placed below remains reachable from the top (D6).
        if (m_underlying_platform)
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);

        return save_disposition{};
    }

    void
    platform::save_journal(pugi::xml_node& journal_node) const
    {
        m_journal->save(journal_node);
    }
} // namespace m::pil::impl::journaling
