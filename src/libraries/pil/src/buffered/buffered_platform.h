// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry_interfaces.h>
#include <m/strings/compare.h>

//
// A buffered platform layer may be an overlay or may be standalone.
//
// Overlay buffered platforms are "deltas" in that they store changes over an
// underlying platform's state.
//
// That underlying platform's state of course may change over the lifetime of
// the overlay but there is no single overarching answer for how to approach
// this issue.
//
// In general, containers for a platform must keep record of changes (new
// items, changes to existing items, deletion of items) and report state
// which at the very least represents the updated state.
//
// The buffered platform implementation may do this by maintaining a copy of
// the container's state in shallow form and report the state by only
// returning the local copy, or by keeping track only of the deltas and
// then merging the state with the underlying platform's state.
//
// What a buffered platform container must be careful to NOT do is to not
// make a full copy of all its contained state, including streams of data and
// copies of all its subcontainers.
//
// Depending on the model here, it may, or may not, maintain copies of
// streams. It's impossible to make a one-size-fits-all recommendation here,
// this is not a forum for designing a filesystem platform, a buffered PIL is
// a tool to aid testing and certain limited production scenarios.
//
// For the Windows Registry, for example, a buffered registry key may
// well keep copies of all the values. Or it may keep copies of all values
// previously fetched.
//
// But for a filesystem directory, it would be unreasonable to expect that a
// buffered PIL directory would keep in-memory copy of files.
//
// Breaking, 7/9/2025: Because of the "stateless" enumeration model, it's
// not feasible to have a delta-only based in memory key state.
//
// I had been thinking that there were three models. The key has a list of
// all its children ("mirrored") but there was an underlying registry,
// there was no underlying registry, and then there was a model where
// the in-memory list was a delta over the underlying registry state,
// a list of additions and subtractions.
//
// The problem is enumeration. If you had a formal enumerator object it
// could work - I will explain. The problem is that when enumerating
// you don't know until you are done which are which. You could re-form
// a list of the added and deleted keys/values on every call into
// enumeration, but this would seem extremely expensive. (The expense would
// be mitigated with an enumeration context but the registry API today
// has none because today it is modeled directly on the Windows registry
// API with some simplifications.)
//

namespace m::pil::impl::buffered
{
    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;
        platform(std::shared_ptr<iplatform> const& underlying_platform);
        platform(std::shared_ptr<iplatform>&& underlying_platform) noexcept;
        platform(platform&& other) noexcept = delete;
        platform(platform const&) = delete;
        ~platform()               = default;

        platform&
        operator=(platform&& other) noexcept = delete;

        platform&
        operator=(platform const&) = delete;

        void
        swap(platform& other) noexcept = delete;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

    protected:
        bool
        overlaid() const
        {
            return static_cast<bool>(m_underlying_platform);
        }

        std::shared_ptr<iplatform> m_underlying_platform;
    };

} // namespace m::pil::impl::buffered
