// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <m/pil/file_path.h>
#include <m/strings/compare.h>
#include <m/utility/exception.h>

using namespace std::string_view_literals;

namespace m::pil
{
    namespace
    {
        using view_type = std::basic_string_view<char16_t>;

        constexpr char16_t drive_colon = u':';
        constexpr char16_t device_dot  = u'.';
        constexpr char16_t query_mark  = u'?';

        constexpr bool
        is_separator(char16_t c) noexcept
        {
            return c == file_preferred_separator || c == file_posix_separator;
        }

        // The extended-length ("\\?\") and device ("\\.\") prefixes are recognized
        // only with literal backslashes; forward slashes do not introduce them.
        constexpr bool
        is_windows_separator(char16_t c) noexcept
        {
            return c == file_preferred_separator;
        }

        constexpr bool
        is_ascii_letter(char16_t c) noexcept
        {
            return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z');
        }

        constexpr char16_t
        ascii_upper(char16_t c) noexcept
        {
            return (c >= u'a' && c <= u'z') ? static_cast<char16_t>(c - (u'a' - u'A')) : c;
        }

        // Index of the first separator at or after `from`, or s.size() if none.
        constexpr std::size_t
        find_separator(view_type s, std::size_t from) noexcept
        {
            for (std::size_t i = from; i < s.size(); ++i)
                if (is_separator(s[i]))
                    return i;
            return s.size();
        }

        // Given that `s` begins with the 4-char "\\?\" prefix, does it continue
        // with the case-insensitive "UNC\" token that distinguishes
        // "\\?\UNC\server\share" from a plain "\\?\..." path?
        constexpr bool
        has_extended_unc_token(view_type s) noexcept
        {
            constexpr std::size_t prefix_len = 4; // "\\?\"
            constexpr std::size_t token_len  = 3; // "UNC"
            if (s.size() < prefix_len + token_len + 1)
                return false;
            return ascii_upper(s[prefix_len + 0]) == u'U' && ascii_upper(s[prefix_len + 1]) == u'N' &&
                   ascii_upper(s[prefix_len + 2]) == u'C' &&
                   is_windows_separator(s[prefix_len + token_len]);
        }

        // Parse the leading root of `s`. Returns the root kind and the number of
        // characters of `s` that constitute the root text (including any separator
        // that terminates the root). The remainder, s.substr(len), is the relative
        // portion. No normalization happens here — the input is classified as-is so
        // that any input round-trips through native() (M-FS-PATH-1); canonical form
        // is M-FS-PATH-2.
        std::pair<file_root_kind, std::size_t>
        parse_root(view_type s) noexcept
        {
            auto const n = s.size();

            if (n == 0)
                return {file_root_kind::none, 0};

            // Drive root: <letter> ':' [ separator ]
            if (n >= 2 && is_ascii_letter(s[0]) && s[1] == drive_colon)
            {
                std::size_t len = 2;
                if (n >= 3 && is_separator(s[2]))
                    ++len; // absorb the single terminating separator (drive-absolute)
                return {file_root_kind::drive, len};
            }

            // All other rooted forms start with two leading separators.
            if (n >= 2 && is_separator(s[0]) && is_separator(s[1]))
            {
                // Extended-length ("\\?\") and device ("\\.\") namespaces require
                // literal backslashes; their remainder is opaque (prefix-only root).
                if (n >= 4 && is_windows_separator(s[0]) && is_windows_separator(s[1]) &&
                    is_windows_separator(s[3]))
                {
                    if (s[2] == query_mark)
                    {
                        constexpr std::size_t extended_prefix_len     = 4; // "\\?\"
                        constexpr std::size_t extended_unc_prefix_len = 8; // "\\?\UNC\"
                        if (has_extended_unc_token(s))
                            return {file_root_kind::extended_unc, extended_unc_prefix_len};
                        return {file_root_kind::extended, extended_prefix_len};
                    }
                    if (s[2] == device_dot)
                    {
                        constexpr std::size_t device_prefix_len = 4; // "\\.\"
                        return {file_root_kind::device, device_prefix_len};
                    }
                }

                // Otherwise UNC "\\server\share": the root spans the two leading
                // separators, the server, the separator, and the share, plus the
                // single separator that terminates the share if present.
                std::size_t const server_end = find_separator(s, 2);
                if (server_end >= n)
                    return {file_root_kind::unc, n}; // "\\server" — incomplete UNC

                std::size_t const share_start = server_end + 1;
                std::size_t const share_end   = find_separator(s, share_start);
                std::size_t const len         = (share_end < n) ? share_end + 1 : share_end;
                return {file_root_kind::unc, len};
            }

            // Single leading separator: POSIX absolute root (the separator itself).
            if (is_separator(s[0]))
                return {file_root_kind::posix, 1};

            // No root => relative path.
            return {file_root_kind::none, 0};
        }

        // Single source of truth for "is this root fully qualified (absolute)?",
        // shared by file_root::is_fully_qualified and file_path::is_absolute.
        bool
        root_is_fully_qualified(file_root_kind kind, view_type text) noexcept
        {
            switch (kind)
            {
            case file_root_kind::none:
                return false;
            case file_root_kind::drive:
                return !text.empty() && is_separator(text.back());
            case file_root_kind::posix:
            case file_root_kind::unc:
            case file_root_kind::device:
            case file_root_kind::extended:
            case file_root_kind::extended_unc:
                return true;
            }
            return false;
        }

        // Split `rem` into non-empty components. `/` is always a separator; `\` is
        // a separator only on the Windows surface (on POSIX it is an ordinary
        // filename character). Empty components — the product of repeated
        // separators — are dropped, which is how separator collapsing happens.
        std::vector<view_type>
        split_segments(view_type rem, bool backslash_is_separator)
        {
            std::vector<view_type> out;
            std::size_t            start = 0;
            for (std::size_t i = 0; i <= rem.size(); ++i)
            {
                bool const at_end = (i == rem.size());
                bool const sep =
                    !at_end && (rem[i] == file_posix_separator ||
                                (backslash_is_separator && rem[i] == file_preferred_separator));
                if (at_end || sep)
                {
                    if (i > start)
                        out.push_back(rem.substr(start, i - start));
                    start = i + 1;
                }
            }
            return out;
        }

        // Resolve "." and ".." lexically. A ".." that would pop past the start of
        // an absolute (fully qualified) path underflows the root and is rejected
        // (D11). In a relative path leading ".." segments are preserved, since
        // they are meaningful against an unknown base.
        std::vector<view_type>
        resolve_dot_segments(std::vector<view_type> const& segments, bool absolute)
        {
            std::vector<view_type> out;
            for (auto const& seg: segments)
            {
                if (seg == u"."sv)
                    continue;
                if (seg == u".."sv)
                {
                    if (!out.empty() && out.back() != u".."sv)
                        out.pop_back();
                    else if (absolute)
                        throw m::invalid_parameter("file_path: '..' underflows the root");
                    else
                        out.push_back(seg);
                }
                else
                {
                    out.push_back(seg);
                }
            }
            return out;
        }

        // Join an already-parsed root with resolved segments. The root text
        // already carries the boundary separator when one is needed (e.g. "C:\",
        // "\\server\share\"); the only non-separator-terminated root that takes
        // segments is the drive-relative "C:" form, which intentionally abuts its
        // first segment with no separator ("C:foo").
        std::u16string
        join_root_and_segments(view_type root_text, std::vector<view_type> const& segments,
                               char16_t separator)
        {
            std::u16string result(root_text);
            for (std::size_t i = 0; i < segments.size(); ++i)
            {
                if (i > 0)
                    result += separator;
                result.append(segments[i]);
            }
            return result;
        }

        std::u16string
        canonicalize_windows(view_type v)
        {
            // Extended-length paths are verbatim (D11): the prefix is recognized
            // but nothing past it is touched.
            {
                auto const [kind, root_len] = parse_root(v);
                if (kind == file_root_kind::extended || kind == file_root_kind::extended_unc)
                    return std::u16string(v);
            }

            // Normalize every separator to the preferred backslash, then re-parse
            // the root on the normalized text.
            std::u16string buffer(v);
            for (auto& c: buffer)
                if (c == file_posix_separator)
                    c = file_preferred_separator;

            view_type const bv          = buffer;
            auto const [kind, root_len] = parse_root(bv);
            view_type const root_text   = bv.substr(0, root_len);
            view_type const remainder   = bv.substr(root_len);

            auto const segments = resolve_dot_segments(
                split_segments(remainder, /*backslash_is_separator*/ true),
                root_is_fully_qualified(kind, root_text));

            return join_root_and_segments(root_text, segments, file_preferred_separator);
        }

        std::u16string
        canonicalize_posix(view_type v)
        {
            // POSIX recognizes only the single "/" root; collapse any run of
            // leading slashes to one. A backslash is an ordinary character here.
            bool const     absolute = !v.empty() && v.front() == file_posix_separator;
            std::u16string root_text;
            view_type      remainder = v;
            if (absolute)
            {
                root_text = std::u16string(1, file_posix_separator);
                std::size_t i = 0;
                while (i < v.size() && v[i] == file_posix_separator)
                    ++i;
                remainder = v.substr(i);
            }

            auto const segments =
                resolve_dot_segments(split_segments(remainder, /*backslash_is_separator*/ false),
                                     absolute);

            return join_root_and_segments(root_text, segments, file_posix_separator);
        }

        // The separator that joins a child onto `path`: POSIX paths join with "/",
        // everything else with the preferred backslash.
        constexpr char16_t
        join_separator_for(file_root_kind kind) noexcept
        {
            return kind == file_root_kind::posix ? file_posix_separator : file_preferred_separator;
        }
    } // namespace

    bool
    file_root::is_fully_qualified() const noexcept
    {
        return root_is_fully_qualified(m_kind, m_text.view());
    }

    file_path::file_path(string_type&& str) { assign(str.view()); }

    file_path::file_path(view_type str) { assign(str); }

    file_path::file_path(file_path const& other):
        m_value(other.m_value), m_root_kind(other.m_root_kind), m_root_length(other.m_root_length)
    {}

    file_path::file_path(file_path&& other) noexcept { swap(other); }

    void
    file_path::assign(view_type in)
    {
        auto const [kind, len] = parse_root(in);
        m_value                = in;
        m_root_kind            = kind;
        m_root_length          = len;
    }

    file_path&
    file_path::operator=(file_path const& other)
    {
        m_value       = other.m_value;
        m_root_kind   = other.m_root_kind;
        m_root_length = other.m_root_length;
        return *this;
    }

    file_path&
    file_path::operator=(file_path&& other) noexcept
    {
        file_path tmp(std::move(other));
        swap(tmp);
        return *this;
    }

    file_path&
    file_path::operator=(view_type str)
    {
        assign(str);
        return *this;
    }

    bool
    file_path::operator==(file_path const& other) const
    {
        // Exact textual equality (case-sensitive). Ordinal case-insensitive
        // comparison is a separate concern layered on top per D12 (M-FS-PATH-3),
        // mirroring how key_path equality is exact while lookups use a comparator.
        return m_value == other.m_value;
    }

    file_path::value_type const*
    file_path::c_str() const noexcept
    {
        return m_value.c_str();
    }

    file_path::string_type const&
    file_path::native() const& noexcept
    {
        return m_value;
    }

    file_path::operator string_type() const { return m_value; }

    file_path::string_type
    file_path::string() const
    {
        return m_value;
    }

    void
    file_path::clear()
    {
        m_value       = string_type{};
        m_root_kind   = file_root_kind::none;
        m_root_length = 0;
    }

    void
    file_path::swap(file_path& other) noexcept
    {
        using std::swap;
        swap(m_value, other.m_value);
        swap(m_root_kind, other.m_root_kind);
        swap(m_root_length, other.m_root_length);
    }

    file_root
    file_path::root() const
    {
        return file_root(m_root_kind, m_value.substr(0, m_root_length));
    }

    file_path::string_type
    file_path::relative_path() const
    {
        return m_value.substr(m_root_length);
    }

    bool
    file_path::is_absolute() const noexcept
    {
        return root_is_fully_qualified(m_root_kind, m_value.view().substr(0, m_root_length));
    }

    file_path
    file_path::lexically_normal(path_surface surface) const
    {
        std::u16string const normalized = (surface == path_surface::windows)
                                              ? canonicalize_windows(m_value.view())
                                              : canonicalize_posix(m_value.view());
        return file_path(view_type{normalized});
    }

    std::pair<std::optional<file_path>, file_path>
    file_path::split_parent_path_and_leaf_name() const
    {
        view_type const full      = m_value.view();
        view_type const root_text = full.substr(0, m_root_length);
        view_type       relative  = full.substr(m_root_length);

        bool const backslash_is_separator = (m_root_kind != file_root_kind::posix);
        auto const is_sep = [backslash_is_separator](char16_t c) noexcept {
            return c == file_posix_separator ||
                   (backslash_is_separator && c == file_preferred_separator);
        };

        // A trailing separator past the root names no leaf; ignore it.
        while (!relative.empty() && is_sep(relative.back()))
            relative.remove_suffix(1);

        if (relative.empty())
            return {std::nullopt, file_path()};

        std::size_t last_sep = relative.size(); // sentinel: no separator
        for (std::size_t i = relative.size(); i-- > 0;)
        {
            if (is_sep(relative[i]))
            {
                last_sep = i;
                break;
            }
        }

        view_type const leaf_view =
            (last_sep == relative.size()) ? relative : relative.substr(last_sep + 1);
        file_path leaf(view_type{leaf_view});

        view_type parent_rel =
            (last_sep == relative.size()) ? view_type{} : relative.substr(0, last_sep);
        while (!parent_rel.empty() && is_sep(parent_rel.back()))
            parent_rel.remove_suffix(1);

        if (parent_rel.empty())
        {
            if (m_root_length == 0)
                return {std::nullopt, std::move(leaf)};
            return {file_path(view_type{root_text}), std::move(leaf)};
        }

        std::u16string parent_text(root_text);
        parent_text.append(parent_rel);
        return {file_path(view_type{parent_text}), std::move(leaf)};
    }

    file_path
    file_path::parent_path() const
    {
        return split_parent_path_and_leaf_name().first.value_or(file_path());
    }

    bool
    file_path::has_parent_path() const
    {
        return split_parent_path_and_leaf_name().first.has_value();
    }

    file_path&
    file_path::operator/=(file_path const& rhs)
    {
        // Appending a fully qualified path replaces this path entirely.
        if (rhs.is_absolute())
        {
            *this = rhs;
            return *this;
        }

        view_type const rhs_value = rhs.m_value.view();
        if (rhs_value.empty())
            return *this;

        view_type const lhs_value = m_value.view();
        if (lhs_value.empty())
        {
            *this = rhs;
            return *this;
        }

        std::u16string combined(lhs_value);
        if (!is_separator(lhs_value.back()) && !is_separator(rhs_value.front()))
            combined += join_separator_for(m_root_kind);
        combined.append(rhs_value);
        *this = file_path(view_type{combined});
        return *this;
    }

    file_path
    file_path::operator/(file_path const& rhs) const
    {
        file_path result(*this);
        result /= rhs;
        return result;
    }

    bool
    file_path::precedes(file_path const& other, path_surface surface) const
    {
        view_type const lhs = m_value.view();
        view_type const rhs = other.m_value.view();

        if (surface == path_surface::windows)
        {
            // Ordinal case-insensitive (CompareStringOrdinal with case folding).
            m::case_insensitive_less<std::u16string_view> const less{};
            return less(lhs, rhs);
        }

        // POSIX: ordinal case-sensitive. u16string_view's operator< is an
        // ordinal (code-unit) lexicographic compare.
        return lhs < rhs;
    }

    bool
    file_path::equivalent(file_path const& other, path_surface surface) const
    {
        view_type const lhs = m_value.view();
        view_type const rhs = other.m_value.view();

        if (surface == path_surface::windows)
        {
            m::case_insensitive_less<std::u16string_view> const less{};
            return !less(lhs, rhs) && !less(rhs, lhs);
        }

        return lhs == rhs;
    }
} // namespace m::pil
