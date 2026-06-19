// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <stdexcept>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/pil/registry.h>

#include "journaling.h"

namespace m::pil::impl::journaling
{
    registry::registry(std::shared_ptr<iregistry> const& underlying_registry,
                       std::shared_ptr<journal> const&   journal_ptr):
        m_underlying_registry(underlying_registry), m_journal(journal_ptr)
    {
        M_INTERNAL_ERROR_CHECK(m_underlying_registry.get() != nullptr);
        M_INTERNAL_ERROR_CHECK(m_journal.get() != nullptr);
    }

    iregistry::open_predefined_key_disposition
    registry::open_predefined_key(open_predefined_key_flags flags,
                                  predefined_key            pk,
                                  sam,
                                  std::shared_ptr<m::pil::ikey>& returned_key)
    {
        if (flags != open_predefined_key_flags{})
            throw std::runtime_error("Invalid flags to call to iregistry::open_predefined_key()");

        auto lock = std::unique_lock(m_mutex);

        auto find_location = m_predefined_keys.find(pk);
        if (find_location != m_predefined_keys.end())
        {
            returned_key = find_location->second;
            return open_predefined_key_disposition{};
        }

        std::shared_ptr<ikey> underlying_predefined_key;
        if (m_underlying_registry)
            underlying_predefined_key = m_underlying_registry->open_predefined_key(pk);

        auto const [insertion_location, inserted] = m_predefined_keys.emplace(std::make_pair(
            pk, std::make_shared<key>(std::move(underlying_predefined_key), m_journal)));
        M_INTERNAL_ERROR_CHECK(inserted);

        returned_key = insertion_location->second;

        return open_predefined_key_disposition{};
    }

    iregistry::monitor_disposition
    registry::monitor(monitor_flags                               flags,
                      std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor)
    {
        // Monitoring is a read-side capability; the journal records mutations
        // only. Forward the underlying monitor directly.
        return m_underlying_registry->monitor(flags, returned_registry_monitor);
    }
} // namespace m::pil::impl::journaling
