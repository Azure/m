// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/security_attributes.h>
#include <m/pil/http_contract_interfaces.h>
#include <m/pil/http_listener_interfaces.h>
#include <m/pil/webcore_interfaces.h>
#include <m/strings/convert.h>
#include <m/utility/enum_operations.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include <pugixml.hpp>

//
// In the platform isolation layer, a "platform" represents a stack of whatever various layers
// are working together to provide a functional mock platform.
//
// The platform object itself provides a minimal interface which only gives access to root
// objects of the various types.
//

namespace m::pil
{
    struct iplatform
    {
        virtual ~iplatform() {}

        //
        //  get_registry
        //

        enum class get_registry_flags : uint64_t
        {
        };

        enum class get_registry_result_code : uint32_t
        {
        };

        enum class get_registry_result_flags : uint32_t
        {
        };

        using get_registry_disposition =
            disposition<get_registry_result_code, get_registry_result_flags>;

        virtual get_registry_disposition
        get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry) = 0;

        std::shared_ptr<m::pil::iregistry>
        get_registry()
        {
            std::shared_ptr<iregistry> returned_registry;
            auto const                 d = get_registry(get_registry_flags{}, returned_registry);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_registry;
        }

        //
        //  get_filesystem
        //

        enum class get_filesystem_flags : uint64_t
        {
        };

        enum class get_filesystem_result_code : uint32_t
        {
        };

        enum class get_filesystem_result_flags : uint32_t
        {
        };

        using get_filesystem_disposition =
            disposition<get_filesystem_result_code, get_filesystem_result_flags>;

        //
        // Unlike get_registry (a pure virtual every provider must implement),
        // get_filesystem has a default that yields a null provider: a filesystem
        // that resolves through the stack but whose operations are not yet
        // implemented (until M-FS-DIRECT). A provider with a live filesystem
        // overrides this; the existing registry-only providers inherit the
        // default and need no changes.
        //
        virtual get_filesystem_disposition
        get_filesystem(get_filesystem_flags, std::shared_ptr<ifilesystem>& returned_filesystem)
        {
            returned_filesystem = std::make_shared<null_filesystem>();
            return {};
        }

        std::shared_ptr<m::pil::ifilesystem>
        get_filesystem()
        {
            std::shared_ptr<ifilesystem> returned_filesystem;
            auto const d = get_filesystem(get_filesystem_flags{}, returned_filesystem);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_filesystem;
        }

        //
        //  get_webcore
        //
        //  Returns the HWC (Hostable Web Core) engine surface (D-HWC-1, D-HWC-2).
        //  Unlike get_registry (a pure virtual), get_webcore has a default that
        //  yields a null provider whose operations are not-implemented (until
        //  a provider overrides it, e.g. the direct/Windows platform in
        //  M-HWC-DIRECT). This mirrors get_filesystem so existing providers
        //  need no change.
        //

        enum class get_webcore_flags : uint64_t
        {
        };

        enum class get_webcore_result_code : uint32_t
        {
        };

        enum class get_webcore_result_flags : uint32_t
        {
        };

        using get_webcore_disposition =
            disposition<get_webcore_result_code, get_webcore_result_flags>;

        virtual get_webcore_disposition
        get_webcore(get_webcore_flags, std::shared_ptr<iwebcore>& returned_webcore)
        {
            returned_webcore = std::make_shared<null_webcore>();
            return {};
        }

        std::shared_ptr<m::pil::iwebcore>
        get_webcore()
        {
            std::shared_ptr<iwebcore> returned_webcore;
            auto const d = get_webcore(get_webcore_flags{}, returned_webcore);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_webcore;
        }

        //
        //  get_http_listener
        //
        //  Returns the HTTP listener isolation surface (D-HWC-6). Like
        //  get_webcore, this has a default that yields a null provider whose
        //  operations are not-implemented (until a provider overrides it,
        //  e.g. the Tier A or Tier B implementations in M-HWC-HTTP).
        //

        enum class get_http_listener_flags : uint64_t
        {
        };

        enum class get_http_listener_result_code : uint32_t
        {
        };

        enum class get_http_listener_result_flags : uint32_t
        {
        };

        using get_http_listener_disposition =
            disposition<get_http_listener_result_code, get_http_listener_result_flags>;

        virtual get_http_listener_disposition
        get_http_listener(get_http_listener_flags, std::shared_ptr<ihttp_listener>& returned_http_listener)
        {
            returned_http_listener = std::make_shared<null_http_listener>();
            return {};
        }

        std::shared_ptr<m::pil::ihttp_listener>
        get_http_listener()
        {
            std::shared_ptr<ihttp_listener> returned_http_listener;
            auto const d = get_http_listener(get_http_listener_flags{}, returned_http_listener);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_http_listener;
        }

        //
        //  get_http_contract
        //
        //  Returns the HTTP contract (OpenAPI/Swagger) surface (D-HWC-8,
        //  D-HWC-9). Like get_webcore / get_http_listener, this has a default
        //  that yields a null provider whose operations are not-implemented
        //  (until a provider overrides it, e.g. the live validating provider in
        //  M-HWC-CONTRACT-VALIDATE).
        //

        enum class get_http_contract_flags : uint64_t
        {
        };

        enum class get_http_contract_result_code : uint32_t
        {
        };

        enum class get_http_contract_result_flags : uint32_t
        {
        };

        using get_http_contract_disposition =
            disposition<get_http_contract_result_code, get_http_contract_result_flags>;

        virtual get_http_contract_disposition
        get_http_contract(get_http_contract_flags, std::shared_ptr<ihttp_contract>& returned_http_contract)
        {
            returned_http_contract = std::make_shared<null_http_contract>();
            return {};
        }

        std::shared_ptr<m::pil::ihttp_contract>
        get_http_contract()
        {
            std::shared_ptr<ihttp_contract> returned_http_contract;
            auto const d = get_http_contract(get_http_contract_flags{}, returned_http_contract);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_http_contract;
        }

        //
        // save
        //

        enum class save_flags : uint64_t
        {
            //
        };

        enum class save_contents
        {
            change_log,
        };

        enum class save_result_code : uint32_t
        {
            //
        };

        enum class save_result_flags : uint32_t
        {
            //
        };

        using save_disposition = disposition<save_result_code, save_result_flags>;

        virtual save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) = 0;

        void
        save(save_contents contents, pugi::xml_node& platform_element)
        {
            auto const d = save(save_flags{}, contents, platform_element);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        // save_diagnostic_log
        //
        // D6: the requested-vs-done diagnostic trace is never part of a
        // persisted <Platform>. A layer that records such a trace (the logging
        // layer) writes it here, into a separate side artifact. Layers with no
        // diagnostic log leave the node untouched; the default is a no-op.
        //

        virtual save_disposition
        save_diagnostic_log(save_flags, pugi::xml_node&)
        {
            return save_disposition{};
        }

        void
        save_diagnostic_log(pugi::xml_node& diagnostic_element)
        {
            auto const d = save_diagnostic_log(save_flags{}, diagnostic_element);
            M_INTERNAL_ERROR_CHECK(!d);
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_registry_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_registry_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_filesystem_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_filesystem_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_webcore_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_webcore_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_http_listener_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_http_listener_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_http_contract_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_http_contract_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::save_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::save_result_flags);

} // namespace m::pil
