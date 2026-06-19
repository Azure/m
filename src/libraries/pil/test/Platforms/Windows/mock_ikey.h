// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/utility/exception.h>

namespace m::pil::test
{
    // A minimal, fully controllable mock of m::pil::ikey for deterministically
    // exercising the buffered layer's best-effort whole-key capture (M-PS-2). It
    // implements only the read paths the capture touches — query_information_key,
    // enumerate_keys, enumerate_value_names_and_types, get_value_size,
    // get_value_type, get_value — and lets a test script:
    //
    //   * a sequence of last_write_time values returned by successive
    //     query_information_key calls, so the capture's before/after bracket can
    //     be made to observe a "torn read" (a stamp that changed across the
    //     bracket) and then stabilize; and
    //   * a set of values, any of which can be marked "vanished" so its
    //     get_value_size / get_value throw m::not_found, modelling a value that
    //     disappeared from the underlying registry between enumeration and load.
    //
    // It records how many capture passes occurred (one
    // enumerate_value_names_and_types call per attempt) so a test can assert the
    // bounded retry fired the expected number of times. All mutating ikey methods
    // throw, since capture never calls them.
    class mock_ikey : public ikey
    {
    public:
        struct value_spec
        {
            value_name_string_type m_name;
            reg_value_type         m_type{reg_value_type::binary};
            std::vector<std::byte> m_data;
            // When true, get_value_size / get_value for this value throw
            // m::not_found, modelling a value that vanished between enumeration
            // and load.
            bool m_vanished{false};
        };

        // last_write_times is the scripted sequence returned by successive
        // query_information_key calls. When exhausted, the last element is
        // returned for all further calls. An empty sequence yields a fixed stamp.
        mock_ikey(std::vector<time_point_type> last_write_times,
                  std::vector<value_name_string_type> subkey_names,
                  std::vector<value_spec>             values):
            m_last_write_times(std::move(last_write_times)),
            m_subkey_names(std::move(subkey_names)),
            m_values(std::move(values))
        {}

        // Number of whole-key capture passes the buffered layer ran against this
        // mock (one per enumerate_value_names_and_types call). Equals the number
        // of capture attempts, so a value > 1 proves a torn-read retry fired.
        unsigned
        capture_pass_count() const noexcept
        {
            return m_capture_passes;
        }

        // --- read paths exercised by capture ---

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags,
                              std::size_t&     subkey_count,
                              std::size_t&     value_count,
                              std::size_t&     security_descriptor_size,
                              time_point_type& last_write_time) override
        {
            subkey_count             = m_subkey_names.size();
            value_count              = m_values.size();
            security_descriptor_size = 0;

            if (m_last_write_times.empty())
            {
                last_write_time = (time_point_type::min)();
            }
            else
            {
                auto const i = (m_lwt_index < m_last_write_times.size())
                                   ? m_lwt_index
                                   : (m_last_write_times.size() - 1);
                last_write_time = m_last_write_times[i];
                ++m_lwt_index;
            }

            return query_information_key_disposition{};
        }

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags,
                       std::size_t                                    index,
                       std::span<pil::key_path, std::dynamic_extent>& key_names) override
        {
            std::size_t written{};
            while (written < key_names.size() && (index + written) < m_subkey_names.size())
            {
                key_names[written] = pil::key_path(m_subkey_names[index + written].view());
                ++written;
            }
            key_names = key_names.subspan(0, written);
            return enumerate_keys_disposition{};
        }

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(
            ikey::enumerate_value_names_and_types_flags,
            std::size_t                                                            index,
            std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span)
            override
        {
            // One capture pass = one enumeration sweep starting at index 0.
            if (index == 0)
                ++m_capture_passes;

            std::size_t written{};
            while (written < values_span.size() && (index + written) < m_values.size())
            {
                auto const& spec       = m_values[index + written];
                values_span[written]   = enumerate_value_names_and_types_value(spec.m_name,
                                                                             spec.m_type);
                ++written;
            }
            values_span = values_span.subspan(0, written);
            return enumerate_value_names_and_types_disposition{};
        }

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags,
                       value_name_string_type const& value_name,
                       std::size_t&                  size) override
        {
            auto const& spec = require_value(value_name);
            size             = spec.m_data.size();
            return get_value_size_disposition{};
        }

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags,
                       value_name_string_type const& value_name,
                       reg_value_type&               type) override
        {
            auto const& spec = require_value(value_name);
            type             = spec.m_type;
            return get_value_type_disposition{};
        }

        ikey::get_value_disposition
        get_value(ikey::get_value_flags,
                  value_name_string_type const& value_name,
                  reg_value_type&               type,
                  std::span<std::byte>&         value,
                  std::optional<std::size_t>&   new_bytes_required) override
        {
            auto const& spec   = require_value(value_name);
            new_bytes_required = std::nullopt;

            if (value.size() < spec.m_data.size())
            {
                new_bytes_required = spec.m_data.size();
                return get_value_disposition{};
            }

            for (std::size_t i = 0; i < spec.m_data.size(); ++i)
                value[i] = spec.m_data[i];

            value = value.subspan(0, spec.m_data.size());
            type  = spec.m_type;
            return get_value_disposition{};
        }

        // --- paths capture never touches ---

        ikey::create_key_disposition
        create_key(ikey::create_key_flags,
                   pil::key_path const&,
                   sam,
                   std::optional<security_attributes>,
                   std::shared_ptr<ikey>&) override
        {
            throw m::not_supported("mock_ikey::create_key");
        }

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags, pil::key_path const&, sam) override
        {
            throw m::not_supported("mock_ikey::delete_key");
        }

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags, std::optional<pil::key_path> const&) override
        {
            throw m::not_supported("mock_ikey::delete_tree");
        }

        ikey::flush_disposition
        flush(ikey::flush_flags) override
        {
            throw m::not_supported("mock_ikey::flush");
        }

        ikey::open_key_disposition
        open_key(ikey::open_key_flags,
                 std::optional<pil::key_path> const&,
                 sam,
                 std::shared_ptr<ikey>&,
                 std::error_code&) override
        {
            throw m::not_supported("mock_ikey::open_key");
        }

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags,
                   std::optional<pil::key_path> const&,
                   pil::key_path const&) override
        {
            throw m::not_supported("mock_ikey::rename_key");
        }

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags, value_name_string_type const&) override
        {
            throw m::not_supported("mock_ikey::delete_value");
        }

        ikey::set_value_disposition
        set_value(ikey::set_value_flags,
                  value_name_string_type const&,
                  reg_value_type,
                  std::span<std::byte const>) override
        {
            throw m::not_supported("mock_ikey::set_value");
        }

        ikey::get_path_disposition
        get_path(ikey::get_path_flags, pil::key_path&) override
        {
            throw m::not_supported("mock_ikey::get_path");
        }

    private:
        value_spec const&
        require_value(value_name_string_type const& value_name) const
        {
            for (auto const& spec: m_values)
            {
                if (spec.m_name.view() == value_name.view())
                {
                    if (spec.m_vanished)
                        throw m::not_found("mock_ikey: value vanished");
                    return spec;
                }
            }
            throw m::not_found("mock_ikey: unknown value");
        }

        std::vector<time_point_type>        m_last_write_times;
        std::vector<value_name_string_type> m_subkey_names;
        std::vector<value_spec>             m_values;
        std::size_t                         m_lwt_index{0};
        unsigned                            m_capture_passes{0};
    };

} // namespace m::pil::test
