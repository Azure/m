// Copyrght (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <version>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/print/print.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    void
    key::initialize_values_overlay()
    {
        if (!m_underlying_key)
            return;

        std::array<enumerate_value_names_and_types_value, 32>                 evnatv_array;
        std::span<enumerate_value_names_and_types_value, std::dynamic_extent> evnatv_span{
            evnatv_array};

        std::size_t index{};

        for (;;)
        {
            auto const d = m_underlying_key->enumerate_value_names_and_types(
                enumerate_value_names_and_types_flags{}, index, evnatv_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto&& e: evnatv_span)
            {
                value_node vnv{.m_reg_value_type = e.m_reg_value_type,
                               .m_value          = std::nullopt,
                               .m_deleted        = false};

                m_values.emplace(std::move(e.m_value_name), std::move(vnv));
            }

            if (evnatv_span.size() != evnatv_array.size())
                break;

            index += evnatv_array.size();
        }
    }

    ikey::delete_value_disposition
    key::delete_value(ikey::delete_value_flags flags, value_name_string_type const& value_name)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_value", flags);

        auto lock = std::unique_lock(m_mutex);

        auto it = m_values.find(value_name);
        if (it == m_values.end() || it->second.m_deleted)
            throw m::not_found("ikey::delete_value(): value not found");

        it->second.m_value   = std::nullopt;
        it->second.m_deleted = true;

        return delete_value_disposition{};
    }

    ikey::enumerate_value_names_and_types_disposition
    key::enumerate_value_names_and_types(
        ikey::enumerate_value_names_and_types_flags                            flags,
        std::size_t                                                            index,
        std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::enumerate_value_names_and_types", flags);

        auto lock = std::unique_lock(m_mutex);

        std::size_t span_index{};

        auto it = m_values.begin();

        while (it != m_values.end() && span_index < values_span.size())
        {
            // First, if this is a deleted value, skip, without accounting for
            // it in the index decrementing or in the span.
            //
            if (it->second.m_deleted)
            {
                it++;
                continue;
            }

            // Similarly, if we have more indices to skip, do so. It might seem
            // ideal to use the notion that it might be a random access iterator
            // to avoid some of this but we don't know which nodes are
            // tombstones so we kind of have to do this. If this is a real
            // problem perhaps tombstones need to be kept separately? That
            // doesn't seem to prevent the fundamental problem though unless
            // they were somehow sorted last, which is an alternate idea and
            // probably the only way to fix this if it needs to be fixed.
            //
            // Alternate solution: get in chunks where you usually get a
            // single chunk, with 99% of the cases being 1-2 chunks. Let the
            // outliers be outliers.
            //
            if (index > 0)
            {
                it++;
                index--;
                continue;
            }

            auto& item = values_span[span_index];

            item.m_value_name     = it->first;
            item.m_reg_value_type = it->second.m_reg_value_type;

            span_index++;
            it++;
        }

        M_INTERNAL_ERROR_CHECK(span_index <= values_span.size());

        // trim the span down to what we actually populated
        values_span = values_span.subspan(0, span_index);

        return enumerate_value_names_and_types_disposition{};
    }

    ikey::get_value_size_disposition
    key::get_value_size(ikey::get_value_size_flags    flags,
                        value_name_string_type const& value_name,
                        std::size_t&                  size)
    {
        size = 0;

        M_API_PARAMETER_MUST_BE_ZERO("ikey::get_value_size", flags);

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_values.find(value_name);

        if (it == m_values.end() || it->second.m_deleted)
            throw m::not_found("ikey::get_value_size(): value not found");

        load_value_if_not_present(it->first, it->second);

        // Because loading may find that it was deleted in the underlying
        // registry there is another chance to report
        // not found. Maybe load_value_if_not_present()
        // should throw the not found?
        if (it->second.m_deleted)
            throw m::not_found("ikey::get_value_size(): value not found");

        M_INTERNAL_ERROR_CHECK(it->second.m_value.has_value());

        size = it->second.m_value.value().size();
        return get_value_size_disposition{};
    }

    ikey::get_value_type_disposition
    key::get_value_type(ikey::get_value_type_flags flags,
                        value_name_string_type const& value_name,
                        reg_value_type&            type)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::get_value_type", flags);

        type = reg_value_type{};

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_values.find(value_name);

        if (it == m_values.end() || it->second.m_deleted)
            throw m::not_found("ikey::get_value_type(): value not found");

        type = it->second.m_reg_value_type;

        return get_value_type_disposition{};
    }

    ikey::get_value_disposition
    key::get_value(ikey::get_value_flags         flags,
                   value_name_string_type const& value_name,
                   reg_value_type&               type,
                   std::span<std::byte>&         value,
                   std::optional<std::size_t>&   new_bytes_required)
    {
        new_bytes_required = std::nullopt;

        M_API_PARAMETER_MUST_BE_ZERO("ikey::get_value", flags);

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_values.find(value_name);

        if (it == m_values.end() || it->second.m_deleted)
            throw m::not_found("ikey::get_value(): value not found");

        load_value_if_not_present(it->first, it->second);

        // Because loading may find that it was deleted in the underlying
        // registry there is another chance to report
        // not found. Maybe load_value_if_not_present()
        // should throw the not found?
        if (it->second.m_deleted)
            throw m::not_found("ikey::get_value(): value not found");

        // Spell things out with better named locals so everything is simple.
        std::span<std::byte, std::dynamic_extent> output_span{value};
        std::span<std::byte, std::dynamic_extent> input_span{it->second.m_value.value()};

        if (output_span.size() < input_span.size())
        {
            new_bytes_required = input_span.size();
        }
        else
        {
            std::copy_n(input_span.begin(), input_span.size(), output_span.begin());
            value = output_span.subspan(0, input_span.size());
        }

        type = it->second.m_reg_value_type;

        return get_value_disposition{};
    }

    ikey::set_value_disposition
    key::set_value(ikey::set_value_flags         flags,
                   value_name_string_type const& value_name,
                   reg_value_type                type,
                   std::span<std::byte const>    value)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::set_value", flags);

        auto lock = std::unique_lock(m_mutex);

        // It's tricky to avoid making extra copies of the vector and
        // extra searches in the table.
        //
        // The solution is that you have to use .lower_bound() to search
        // the vector the first time. It gives you a "maybe" answer and
        // you have to determine for yourself if the returned iterator
        // meets your requirements for equality.
        //
        // Use lower_bound() because that iterator is a huge time saver
        // for .emplace_hint() if you do have to insert into the tree.
        //

        auto const it = m_values.lower_bound(value_name);
        if (it != m_values.end() && !m_values.key_comp()(value_name, it->first))
        {
            auto const key_comp = m_values.key_comp();
            std::ignore         = key_comp;
            // key_comp is the less_than relationship. Maps don't have
            // "equals", so equality is derived by !a<b and !b<a
            M_DEBUG_INTERNAL_ERROR_CHECK(!key_comp(it->first, value_name) &&
                                         !key_comp(value_name, it->first));

            it->second.m_deleted        = false;
            it->second.m_reg_value_type = type;

            // If there's already a vector in place, avoid overwriting it, but if
            // the optional value isn't present, a new vector must be created.
            if (it->second.m_value.has_value())
            {
#ifdef __cpp_lib_containers_ranges
                it->second.m_value.value().assign_range(value);
#else
                it->second.m_value.value().assign(value.begin(), value.end());
#endif
            }
            else
                it->second.m_value = std::vector(value.begin(), value.end());

            return set_value_disposition{};
        }

        // The call to .upper_bound() didn't return an iterator
        // pointing to an equal key, so now we emplace_hint, assuming the
        // insertion will succeed.

        value_node vnv;
        vnv.m_deleted        = false;
        vnv.m_reg_value_type = type;
        vnv.m_value          = std::vector(value.begin(), value.end());

        m_values.emplace_hint(it, value_name, std::move(vnv));

        return set_value_disposition{};
    }

    void
    key::load_value_if_not_present(value_name_string_type const& value_name, value_node& vnv)
    {
        if (vnv.m_deleted)
            return;

        if (vnv.m_value.has_value())
            return;

        // The only way that it is valid for the value to not be present is if there is
        // an underlying key
        M_INTERNAL_ERROR_CHECK(m_underlying_key);

        std::size_t            value_size = m_underlying_key->get_value_size(value_name);
        std::vector<std::byte> value_vector(value_size);
        value_vector.resize(value_size);

        for (;;)
        {
            reg_value_type             value_type{};
            std::optional<std::size_t> new_bytes_required;

            std::span<std::byte, std::dynamic_extent> value_span{value_vector};

            auto const d = m_underlying_key->get_value(
                ikey::get_value_flags{}, value_name, value_type, value_span, new_bytes_required);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            if (!new_bytes_required)
            {
                value_vector.resize(value_span.size());
                vnv.m_reg_value_type = value_type;

                break;
            }

            M_INTERNAL_ERROR_CHECK(new_bytes_required.value() > value_vector.size());

            value_vector.resize(new_bytes_required.value());
        }
    }

} // namespace m::pil::impl::buffered
