// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/pil/webcore.h>

#include <m/error_handling/macros.h>

#include <utility>

namespace m::pil
{
    //--------------------------------------------------------------------------
    // webcore_instance
    //--------------------------------------------------------------------------

    webcore_instance::webcore_instance(webcore_instance&& other) noexcept:
        m_instance(std::move(other.m_instance))
    {}

    webcore_instance::webcore_instance(std::unique_ptr<iwebcore_instance>&& p) noexcept:
        m_instance(std::move(p))
    {}

    webcore_instance&
    webcore_instance::operator=(webcore_instance&& other) noexcept
    {
        if (this != &other)
        {
            m_instance = std::move(other.m_instance);
        }
        return *this;
    }

    //--------------------------------------------------------------------------
    // webcore_host
    //--------------------------------------------------------------------------

    webcore_host::webcore_host(webcore_host const& other)
    {
        std::lock_guard<std::mutex> guard(other.m_mutex);
        m_webcore = other.m_webcore;
    }

    webcore_host::webcore_host(webcore_host&& other) noexcept
    {
        std::lock_guard<std::mutex> guard(other.m_mutex);
        m_webcore = std::move(other.m_webcore);
    }

    webcore_host::webcore_host(std::shared_ptr<iwebcore>&& sp) noexcept:
        m_webcore(std::move(sp))
    {}

    webcore_host&
    webcore_host::operator=(webcore_host const& other)
    {
        if (this != &other)
        {
            std::scoped_lock lock(m_mutex, other.m_mutex);
            m_webcore = other.m_webcore;
        }
        return *this;
    }

    webcore_host&
    webcore_host::operator=(webcore_host&& other) noexcept
    {
        if (this != &other)
        {
            std::scoped_lock lock(m_mutex, other.m_mutex);
            m_webcore = std::move(other.m_webcore);
        }
        return *this;
    }

    void
    webcore_host::swap(webcore_host& other) noexcept
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        using std::swap;
        swap(m_webcore, other.m_webcore);
    }

    std::shared_ptr<iwebcore>
    webcore_host::get_webcore() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_webcore;
    }

    webcore_instance
    webcore_host::activate(iwebcore::activate_flags flags, activation_request const& request)
    {
        auto sp = get_webcore();
        M_INTERNAL_ERROR_CHECK(sp != nullptr);

        std::unique_ptr<iwebcore_instance> returned_instance;
        auto const d = sp->activate(flags, request, returned_instance);
        M_INTERNAL_ERROR_CHECK(!d);

        return webcore_instance(std::move(returned_instance));
    }

    void
    webcore_host::set_metadata(
        iwebcore::set_metadata_flags flags,
        std::u16string_view          type,
        std::u16string_view          value)
    {
        auto sp = get_webcore();
        M_INTERNAL_ERROR_CHECK(sp != nullptr);

        auto const d = sp->set_metadata(flags, type, value);
        M_INTERNAL_ERROR_CHECK(!d);
    }

} // namespace m::pil
