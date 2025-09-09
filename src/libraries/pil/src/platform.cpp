// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "platform.h"

#include <pugixml.hpp>

#include "pugihelp.h"

using namespace std::string_view_literals;

namespace m::pil
{
    platform
    make_platform(
        make_platform_flags                                                         flags,
        std::initializer_list<std::pair<std::u16string_view, std::u16string_view>>* redirections)
    {
        M_VALIDATE_FLAGS_PARAMETER(
            flags, make_platform_flags::buffer_updates | make_platform_flags::record_modifications);

        impl::create_platform_interface_flags cpif{};

        if (!!(flags & make_platform_flags::buffer_updates))
            cpif |= impl::create_platform_interface_flags::buffer_updates;

        if (!!(flags & make_platform_flags::record_modifications))
            cpif |= impl::create_platform_interface_flags::record_modifications;

        auto sp = impl::create_platform_interface(cpif, redirections);
        return platform(std::move(sp));
    }

    platform::platform(platform&& other) noexcept
    {
        using std::swap;

        swap(m_platform, other.m_platform);
    }

    platform::platform(std::shared_ptr<iplatform>&& sp) noexcept: m_platform(std::move(sp)) {}

    void
    platform::swap(platform& other) noexcept
    {
        using std::swap;
        swap(m_platform, other.m_platform);
    }

    registry_class
    platform::get_registry()
    {
        return registry_class(m_platform->get_registry());
    }

    void
    platform::save(std::filesystem::path const& p, save_contents contents, save_format format)
    {
        M_VALIDATE_PARAMETER(contents, contents == save_contents::change_log);
        M_VALIDATE_PARAMETER(format, format == save_format::xml);

        pugi::xml_document doc;

        auto platform_element = doc.append_child(M_PUGIXML_T("Platform"sv));

        m_platform->save(iplatform::save_contents::change_log, platform_element);

        doc.save_file(m::to_wstring(p.c_str()).c_str());
    }

} // namespace m::pil
