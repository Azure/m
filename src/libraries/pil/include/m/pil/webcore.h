// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <mutex>
#include <utility>

#include <m/pil/webcore_interfaces.h>

//
// Convenience value-wrapper layer over the webcore interfaces. This mirrors
// the filesystem wrappers (filesystem.h: filesystem_class). `webcore_host` is
// the analogue of `filesystem_class` — a value type that owns a shared_ptr to
// the underlying iwebcore and exposes ergonomic, throwing methods.
//
// `webcore_instance` wraps the iwebcore_instance RAII token.
//

namespace m::pil
{
    //
    // webcore_instance
    //
    // RAII token representing a running HWC activation. Releasing the token
    // shuts the instance down (analogous to filesystem_monitor_token).
    //
    class webcore_instance
    {
    public:
        webcore_instance() = default;
        webcore_instance(webcore_instance const&) = delete;
        webcore_instance(webcore_instance&& other) noexcept;
        explicit webcore_instance(std::unique_ptr<iwebcore_instance>&& p) noexcept;
        ~webcore_instance() = default;

        webcore_instance&
        operator=(webcore_instance const&) = delete;
        webcore_instance&
        operator=(webcore_instance&& other) noexcept;

        friend void
        swap(webcore_instance& l, webcore_instance& r) noexcept
        {
            using std::swap;
            swap(l.m_instance, r.m_instance);
        }

        // True when this wrapper holds a running activation token.
        explicit
        operator bool() const noexcept
        {
            return static_cast<bool>(m_instance);
        }

        // Release the underlying token (shut down the instance).
        void
        reset() noexcept
        {
            m_instance.reset();
        }

    private:
        std::unique_ptr<iwebcore_instance> m_instance;
    };

    //
    // webcore_host
    //
    // Value-type wrapper around iwebcore (the HWC engine surface).
    //
    class webcore_host
    {
    public:
        webcore_host() = default;
        webcore_host(webcore_host const& other);
        webcore_host(webcore_host&& other) noexcept;
        explicit webcore_host(std::shared_ptr<iwebcore>&& sp) noexcept;
        ~webcore_host() = default;

        webcore_host&
        operator=(webcore_host const& other);
        webcore_host&
        operator=(webcore_host&& other) noexcept;

        void
        swap(webcore_host& other) noexcept;

        // True when this wrapper refers to a live iwebcore.
        explicit
        operator bool() const noexcept
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            return static_cast<bool>(m_webcore);
        }

        //
        // activate
        //
        // Starts a Hostable Web Core instance with the given request. Only one
        // instance may be active per process (WebCoreActivate contract). The
        // returned webcore_instance is an RAII token — when it is destroyed,
        // the instance shuts down.
        //
        webcore_instance
        activate(iwebcore::activate_flags flags, activation_request const& request);

        // Convenience: default flags.
        webcore_instance
        activate(activation_request const& request)
        {
            return activate(iwebcore::activate_flags{}, request);
        }

        //
        // set_metadata
        //
        // Updates running-instance metadata while the instance is active.
        //
        void
        set_metadata(
            iwebcore::set_metadata_flags flags,
            std::u16string_view          type,
            std::u16string_view          value);

        void
        set_metadata(std::u16string_view type, std::u16string_view value)
        {
            set_metadata(iwebcore::set_metadata_flags{}, type, value);
        }

    private:
        std::shared_ptr<iwebcore>
        get_webcore() const;

        mutable std::mutex         m_mutex;
        std::shared_ptr<iwebcore>  m_webcore;
    };

} // namespace m::pil
