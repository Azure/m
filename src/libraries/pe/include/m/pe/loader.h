// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <m/pe/pe_decoder.h>
#include <m/utility/pointers.h>

using namespace std::string_view_literals;

namespace m::pe::loader
{
    struct pe_not_found_t
    {
        constexpr explicit pe_not_found_t() {}
    };

    inline pe_not_found_t pe_not_found;

    struct trim_graph_t
    {
        constexpr explicit trim_graph_t() {}
    };

    inline trim_graph_t trim_graph;

    using path_resolver_result = std::variant<pe_not_found_t, trim_graph_t, std::filesystem::path>;

    /// <summary>
    /// A m::pe::loader::path_resolver is a function object that takes a path,
    /// which may be complete or partial, and uses whatever context it may to
    /// resolve it to some full path for processing by the loader.
    ///
    /// The typical context that the function/lambda may have captured is the
    /// equivalent of the "PATH" for path searches.
    /// </summary>
    using path_resolver = std::function<path_resolver_result(std::filesystem::path const&)>;

    class loader;
    class pe_record;
    class reference;

    class loader
    {
    public:
        loader(std::initializer_list<path_resolver> path_resolvers);

        loader(loader const&) = delete;

        void
        operator=(loader const&) = delete;

        ~loader() = default;

        void
        resolve(std::filesystem::path const& path);

        std::size_t
        unresolved_count() const;

        template <typename Fn>
        void
        for_each_not_found(Fn&& fn)
        {
            for (auto&& p: m_resolved)
            {
                if (p.second.not_found())
                {
                    std::set<std::wstring> references;

                    for (auto&& r: p.second.inbound_references_ref())
                    {
                        std::ignore = r;
                        // There appears to be a problem in how strings are formed.
                        //
                        // At run time they are showing up with extremely
                        // large lengths. This would seem to indicate either
                        // memory corruption or that there is something
                        // more fundamentally wrong with how they are being
                        // constructed in the first place. (I can't quite
                        // conceive of it offhand but at this moment I have
                        // other fish to fry.)
                        // 
                        // references.insert(r->source()->name());
                    }

                    std::invoke(fn,
                                p.second.name(),
                                references.cbegin(),
                                references.cend());
                }
            }
        }

    protected:
        std::vector<path_resolver> m_path_resolvers;

        path_resolver_result
        resolve_path(std::filesystem::path const& name);

        std::queue<m::not_null<reference const*>> m_pending_references;
        std::map<std::wstring, pe_record>         m_resolved;
        std::size_t                               m_unresolved_count;

        friend class reference;
        friend class pe_record;
    };

    class reference
    {
    public:
        reference(m::not_null<pe_record*> source, std::filesystem::path const& path);

        m::not_null<pe_record*>
        source() const;

        std::wstring
        target_name() const;

        pe_record*
        target() const;

        void
        target(m::not_null<pe_record*> pe) const;

        friend std::strong_ordering
        operator<=>(reference const& l, reference const& r)
        {
            if (l.m_source < r.m_source)
                return std::strong_ordering::less;

            if (l.m_source > r.m_source)
                return std::strong_ordering::greater;

            if (l.m_target_path < r.m_target_path)
                return std::strong_ordering::less;

            if (l.m_target_path > r.m_target_path)
                return std::strong_ordering::greater;

            //
            // The m_target cannot be considered part of the "key"
            // of the reference since it can be changed after creation.
            //

            return std::strong_ordering::equal;
        }

    private:
        m::not_null<pe_record*> m_source;
        mutable pe_record*      m_target;
        std::filesystem::path   m_target_path;
    };

    class pe_record
    {
    public:
        pe_record(std::filesystem::path const& path, m::not_null<loader*> loader);

        //
        // Constructor without a loader context is for "known PEs" where
        // the loader can snip the traversal short.
        //
        pe_record(std::filesystem::path const& path);

        pe_record(std::filesystem::path const& path, pe_not_found_t);

        void
        add_reference(m::not_null<reference const*> reference);

        std::wstring
        name() const;

        bool
        not_found() const;

        std::set<m::not_null<reference const*>> const&
        inbound_references_ref() const;

    protected:
        std::filesystem::path                   m_path;
        bool                                    m_not_found;
        std::set<reference>                     m_outbound_references;
        std::set<m::not_null<reference const*>> m_inbound_references;
        std::unique_ptr<m::pe::decoder>         m_decoder;
    };
} // namespace m::pe::loader
