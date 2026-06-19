// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <m/pil/platform_interfaces.h>
#include <m/pil/webcore_interfaces.h>

#include "webcore_config_platform.h"

namespace m::mwin32_impl
{
    namespace
    {
        //----------------------------------------------------------------------
        // webcore_config_platform — platform decorator that applies webcore config
        //----------------------------------------------------------------------

        class webcore_config_platform final : public m::pil::iplatform
        {
            using iplatform_base = m::pil::iplatform;

        public:
            webcore_config_platform(std::shared_ptr<m::pil::iplatform> underlying_platform,
                                    pilcfg::webcore_config             webcore_cfg);

            ~webcore_config_platform() override = default;

            // iplatform interface
            iplatform_base::get_registry_disposition
            get_registry(iplatform_base::get_registry_flags          flags,
                         std::shared_ptr<m::pil::iregistry>& returned_registry) override;

            iplatform_base::get_filesystem_disposition
            get_filesystem(iplatform_base::get_filesystem_flags          flags,
                           std::shared_ptr<m::pil::ifilesystem>& returned_filesystem) override;

            iplatform_base::get_webcore_disposition
            get_webcore(iplatform_base::get_webcore_flags          flags,
                        std::shared_ptr<m::pil::iwebcore>& returned_webcore) override;

            iplatform_base::get_http_listener_disposition
            get_http_listener(iplatform_base::get_http_listener_flags          flags,
                              std::shared_ptr<m::pil::ihttp_listener>& returned_http_listener) override;

            iplatform_base::save_disposition
            save(iplatform_base::save_flags flags, iplatform_base::save_contents contents, pugi::xml_node& platform_element) override;

            iplatform_base::save_disposition
            save_diagnostic_log(iplatform_base::save_flags flags, pugi::xml_node& diagnostic_element) override;

        private:
            std::shared_ptr<m::pil::iplatform> m_underlying_platform;
            pilcfg::webcore_config             m_webcore_cfg;
            std::shared_ptr<m::pil::iwebcore>  m_webcore;
            std::mutex                         m_mutex;
        };

        //----------------------------------------------------------------------
        // Implementation
        //----------------------------------------------------------------------

        webcore_config_platform::webcore_config_platform(
            std::shared_ptr<m::pil::iplatform> underlying_platform,
            pilcfg::webcore_config             webcore_cfg):
            m_underlying_platform(std::move(underlying_platform)),
            m_webcore_cfg(std::move(webcore_cfg))
        {
        }

        m::pil::iplatform::get_registry_disposition
        webcore_config_platform::get_registry(iplatform_base::get_registry_flags          flags,
                                              std::shared_ptr<m::pil::iregistry>& returned_registry)
        {
            return m_underlying_platform->get_registry(flags, returned_registry);
        }

        m::pil::iplatform::get_filesystem_disposition
        webcore_config_platform::get_filesystem(iplatform_base::get_filesystem_flags          flags,
                                                std::shared_ptr<m::pil::ifilesystem>& returned_filesystem)
        {
            return m_underlying_platform->get_filesystem(flags, returned_filesystem);
        }

        m::pil::iplatform::get_webcore_disposition
        webcore_config_platform::get_webcore(iplatform_base::get_webcore_flags          flags,
                                             std::shared_ptr<m::pil::iwebcore>& returned_webcore)
        {
            returned_webcore.reset();

            if (flags != iplatform_base::get_webcore_flags{})
                throw std::runtime_error("iplatform::get_webcore() called with invalid flags");

            std::lock_guard lock(m_mutex);

            if (!m_webcore)
            {
                // Get the underlying webcore.
                std::shared_ptr<m::pil::iwebcore> underlying_webcore;
                auto d = m_underlying_platform->get_webcore(flags, underlying_webcore);
                (void)d;

                // For now, just store the underlying webcore. Future work will
                // apply the configuration:
                //   - interception mode: wrap with intercepting webcore
                //   - materialization_dir: pass to materializing webcore
                //   - endpoints: configure http_listener namespace mapping
                //   - fault_script: wrap with fault webcore
                //
                // This placeholder stores the config but does not yet apply it;
                // the wrapping infrastructure is in place for future milestones.
                m_webcore = underlying_webcore;
            }

            returned_webcore = m_webcore;
            return iplatform_base::get_webcore_disposition{};
        }

        m::pil::iplatform::get_http_listener_disposition
        webcore_config_platform::get_http_listener(
            iplatform_base::get_http_listener_flags          flags,
            std::shared_ptr<m::pil::ihttp_listener>& returned_http_listener)
        {
            // Forward to underlying; future work will apply endpoint mapping.
            return m_underlying_platform->get_http_listener(flags, returned_http_listener);
        }

        m::pil::iplatform::save_disposition
        webcore_config_platform::save(iplatform_base::save_flags        flags,
                                      iplatform_base::save_contents     contents,
                                      pugi::xml_node&   platform_element)
        {
            // Webcore config is a separate input artifact, not persisted.
            return m_underlying_platform->save(flags, contents, platform_element);
        }

        m::pil::iplatform::save_disposition
        webcore_config_platform::save_diagnostic_log(iplatform_base::save_flags      flags,
                                                     pugi::xml_node& diagnostic_element)
        {
            // Forward so a logging layer below remains reachable.
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);
        }

    } // namespace

    //--------------------------------------------------------------------------
    // Public API
    //--------------------------------------------------------------------------

    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const& underlying_platform,
                         pilcfg::webcore_config const&             webcore_cfg)
    {
        return std::make_shared<webcore_config_platform>(underlying_platform, webcore_cfg);
    }

} // namespace m::mwin32_impl
