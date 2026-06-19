// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <m/sstring/sstring.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

namespace m::pil
{
    // The separator characters recognized in file paths. On Windows both forms
    // are accepted on input (and normalized to the preferred separator during
    // canonicalization, M-FS-PATH-2); on POSIX only the forward slash separates
    // path components. These are named so the parsing logic never spells out a
    // bare separator literal.
    inline constexpr char16_t file_preferred_separator = u'\\';
    inline constexpr char16_t file_posix_separator     = u'/';

    // The family of a path's root (D10). This is an *open* discriminant — new
    // root families can be added without disturbing existing callers — rather
    // than a closed mapping like the registry's predefined_key. `none` denotes a
    // rootless (relative) path.
    enum class file_root_kind : std::uint8_t
    {
        none,         // rootless => relative path
        posix,        // "/" — POSIX absolute root (a bare leading separator)
        drive,        // "C:" — Windows drive root ("C:\" absolute, "C:"/"C:x" drive-relative)
        unc,          // "\\server\share" — Windows UNC share root
        device,       // "\\.\…" — Win32 device namespace
        extended,     // "\\?\…" — extended-length; remainder is verbatim (D11)
        extended_unc, // "\\?\UNC\…" — extended-length UNC; remainder is verbatim (D11)
    };

    // The platform surface whose path rules govern canonicalization (D11) and,
    // later, name comparison (D12). The PIL models a chosen platform that need
    // not be the host, so the surface is an explicit value rather than a compile
    // time decision: `windows` accepts both separators and the drive/UNC/device/
    // extended root families; `posix` uses only `/` (a backslash is an ordinary
    // filename character) and the single `/` root.
    enum class path_surface : std::uint8_t
    {
        windows,
        posix,
    };

    // The root portion of a file_path: a kind discriminant plus the exact root
    // text as it appears in the path (including any separator that terminates the
    // root). Stored case is always preserved — case-insensitivity is a comparison
    // concern (D12), never a normalization of the stored characters.
    class file_root
    {
    public:
        using char_type   = char16_t;
        using string_type = m::basic_sstring<char_type>;
        using view_type   = std::basic_string_view<char_type>;

        file_root() = default;

        file_root(file_root_kind kind, string_type text): m_kind(kind), m_text(std::move(text)) {}

        file_root_kind
        kind() const noexcept
        {
            return m_kind;
        }

        view_type
        text() const noexcept
        {
            return m_text.view();
        }

        // True for a rootless (relative) path.
        bool
        is_none() const noexcept
        {
            return m_kind == file_root_kind::none;
        }

        // True for the extended-length families whose remainder Win32 treats
        // verbatim (D11): no separator/dot normalization is applied past the root.
        bool
        suppresses_normalization() const noexcept
        {
            return m_kind == file_root_kind::extended || m_kind == file_root_kind::extended_unc;
        }

        // True when the root makes the path fully qualified (absolute). A drive
        // root is only fully qualified when terminated by a separator ("C:\");
        // a bare "C:" or "C:foo" is drive-relative.
        bool
        is_fully_qualified() const noexcept;

        bool
        operator==(file_root const& other) const
        {
            return m_kind == other.m_kind && m_text == other.m_text;
        }

        void
        swap(file_root& other) noexcept
        {
            using std::swap;
            swap(m_kind, other.m_kind);
            swap(m_text, other.m_text);
        }

    private:
        file_root_kind m_kind = file_root_kind::none;
        string_type    m_text;
    };

    // A filesystem path: the filesystem-surface analogue of key_path. Like
    // key_path it stores the full path text; unlike key_path its root is an
    // open-ended file_root (D10) rather than a closed predefined_key. M-FS-PATH-1
    // establishes the type, root parsing, and relative/absolute classification;
    // canonicalization and path algebra arrive in M-FS-PATH-2.
    class file_path
    {
    public:
        using char_type   = char16_t;
        using value_type  = char_type;
        using string_type = m::basic_sstring<char_type>;
        using view_type   = std::basic_string_view<char_type>;

        file_path() = default;

        file_path(string_type&& str);

        file_path(file_path const& other);

        file_path(file_path&& other) noexcept;

        file_path(view_type str);

        template <typename CharT>
            requires(m::character<CharT>)
        file_path(std::basic_string_view<CharT> value):
            file_path(string_type{m::to_basic_string_view_t<char_type>(value)})
        {}

        template <typename CharT>
            requires(m::character<CharT>)
        file_path(CharT const* ptr): file_path(std::basic_string_view<CharT>(ptr))
        {}

        file_path&
        operator=(file_path const& other);

        file_path&
        operator=(file_path&& other) noexcept;

        file_path&
        operator=(view_type str);

        bool
        operator==(file_path const& other) const;

        value_type const*
        c_str() const noexcept;

        string_type const&
        native() const& noexcept;

        string_type const&
        native() const&& = delete;

        operator string_type() const;

        string_type
        string() const;

        void
        clear();

        void
        swap(file_path& other) noexcept;

        // The root descriptor (kind + text). A rootless path returns a none root.
        file_root
        root() const;

        file_root_kind
        root_kind() const noexcept
        {
            return m_root_kind;
        }

        // The text following the root. For a rootless path this is the whole
        // value; otherwise it is everything after the root text (which already
        // absorbed the single separator that terminates the root).
        string_type
        relative_path() const;

        // The lexically canonical form of this path for the given surface (D11):
        // separators normalized to the surface's preferred form, repeated
        // separators collapsed, a trailing separator stripped (except a bare
        // root), and "."/".." resolved lexically. A ".." that underflows a fully
        // qualified root throws m::invalid_parameter (it is rejected, never
        // clamped). Inside an extended-length ("\\?\" / "\\?\UNC\") path nothing is
        // normalized — the remainder is preserved verbatim, because Win32 treats
        // such a path as a literally distinct object.
        file_path
        lexically_normal(path_surface surface) const;

        // The path with its final component removed. A path that has no parent
        // (rootless single component, or a bare root) returns an empty path; use
        // split_parent_path_and_leaf_name to distinguish the cases.
        file_path
        parent_path() const;

        bool
        has_parent_path() const;

        // Split into (parent, leaf). The leaf is the final component (the file or
        // directory name); the parent is everything before it. A path with no
        // parent (rootless single component, or a bare root) yields a nullopt
        // parent. Lexical: a single trailing separator past the root is ignored.
        std::pair<std::optional<file_path>, file_path>
        split_parent_path_and_leaf_name() const;

        // Append `rhs` as a child component. Appending a fully qualified path
        // replaces this path (std::filesystem semantics). The joining separator
        // follows this path's convention ("/" for a POSIX root, otherwise "\").
        file_path&
        operator/=(file_path const& rhs);

        file_path
        operator/(file_path const& rhs) const;

        // Name comparison under a surface's case rules (D12). The Windows surface
        // compares ordinal case-insensitively (`m::case_insensitive_less`, i.e.
        // CompareStringOrdinal with case folding); the POSIX surface compares
        // ordinal case-sensitively. The stored case is never altered — native()
        // and string() always return the original casing; only the comparison
        // folds case. Comparison operates on the path text exactly as stored;
        // canonicalize both operands first if path (rather than byte) equivalence
        // is wanted.
        bool
        equivalent(file_path const& other, path_surface surface) const;

        // Strict-weak ordering consistent with equivalent(): two paths are
        // unordered (a precedes b and b precedes a both false) iff they are
        // equivalent under the same surface.
        bool
        precedes(file_path const& other, path_surface surface) const;

        // True when the path carries any root (including a drive-relative root).
        bool
        has_root() const noexcept
        {
            return m_root_kind != file_root_kind::none;
        }

        // True when the path is fully qualified (absolute).
        bool
        is_absolute() const noexcept;

        // True when the path is not fully qualified (rootless, or drive-relative).
        bool
        is_relative() const noexcept
        {
            return !is_absolute();
        }

    private:
        void
        assign(view_type in);

        // m_value holds the entire path text (root + remainder). m_root_kind and
        // m_root_length describe the leading root portion: m_value.substr(0,
        // m_root_length) is the root text and m_value.substr(m_root_length) is the
        // relative remainder. native() == m_value.
        string_type    m_value;
        file_root_kind m_root_kind   = file_root_kind::none;
        std::size_t    m_root_length = 0;
    };
} // namespace m::pil
