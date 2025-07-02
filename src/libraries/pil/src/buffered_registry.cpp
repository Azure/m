// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    registry::registry(std::shared_ptr<iregistry> const& underlying_registry):
        m_underlying_registry(underlying_registry)
    {}

    registry::registry(std::shared_ptr<iregistry>&& underlying_registry) noexcept:
        m_underlying_registry(std::move(underlying_registry))
    {}

    registry::registry(registry&& other) noexcept
    {
        using std::swap;
        swap(m_underlying_registry, other.m_underlying_registry);
        swap(m_predefined_keys, other.m_predefined_keys);
    }

    registry&
    registry::operator=(registry&& other) noexcept
    {
        using std::swap;
        swap(m_underlying_registry, other.m_underlying_registry);
        swap(m_predefined_keys, other.m_predefined_keys);

        return *this;
    }

    bool
    registry::simple_path(std::u16string_view key_path)
    {
        return key_path.contains(pil::uregistry_delimiter);
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

        // there are two axis to consider here: the underlying registry's predefined key, if there
        // is one, and then our wrapper of it.
        std::shared_ptr<ikey> underlying_predefined_key;
        if (m_underlying_registry)
            underlying_predefined_key = m_underlying_registry->open_predefined_key(pk);

        auto const [insertion_location, insertted] = m_predefined_keys.emplace(
            std::make_pair(pk, std::make_shared<key>(std::move(underlying_predefined_key))));
        M_INTERNAL_ERROR_CHECK(insertted);

        returned_key = insertion_location->second;

        return open_predefined_key_disposition{};
    }

} // namespace m::pil::impl::buffered
