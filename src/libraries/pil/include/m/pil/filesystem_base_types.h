// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <m/pil/common.h>
#include <m/pil/file_path.h>
#include <m/sstring/sstring.h>
#include <m/utility/enum_operations.h>

//
// Base value types for the filesystem isolation surface (the second PIL
// surface, modeled on the registry surface). These are surface-neutral: they
// describe a filesystem node abstractly and are reused unchanged by every
// provider and decorator. Unlike the registry surface — which has separate
// subkey and value namespaces — the filesystem namespace is *unified* (D13):
// a child of a directory is exactly one node, either a subdirectory or a file.
//
// File *content* is out of scope for now (D14): a file is modeled as a named
// node carrying metadata only. Byte content and the alternate-data-stream
// sub-namespace are the deferred M-FS-STREAMS milestone.
//

namespace m::pil
{
    // A single path component (a leaf name). Operation arguments that may name a
    // multi-segment relative path use file_path; a directory entry's own name is
    // always one component, so it uses this lighter string type.
    using file_name_char_type   = char16_t;
    using file_name_string_type = m::basic_sstring<file_name_char_type>;
    using file_name_view_type   = std::basic_string_view<file_name_char_type>;

    // The kind of a filesystem node. In the unified namespace (D13) every child
    // of a directory is exactly one of these.
    enum class node_kind : std::uint8_t
    {
        directory,
        file,
    };

    //
    // Attribute flags for a node. The values mirror the Win32 FILE_ATTRIBUTE_*
    // constants so a provider can map them without translation, but the set is
    // surface-neutral and a POSIX provider populates only the subset it can
    // express. Changing any value is a breaking change for persisted snapshots.
    //
    enum class file_attributes : std::uint32_t
    {
        none           = 0x00000000,
        read_only      = 0x00000001, // FILE_ATTRIBUTE_READONLY
        hidden         = 0x00000002, // FILE_ATTRIBUTE_HIDDEN
        system         = 0x00000004, // FILE_ATTRIBUTE_SYSTEM
        directory      = 0x00000010, // FILE_ATTRIBUTE_DIRECTORY
        archive        = 0x00000020, // FILE_ATTRIBUTE_ARCHIVE
        normal         = 0x00000080, // FILE_ATTRIBUTE_NORMAL
        temporary      = 0x00000100, // FILE_ATTRIBUTE_TEMPORARY
        reparse_point  = 0x00000400, // FILE_ATTRIBUTE_REPARSE_POINT
        compressed     = 0x00000800, // FILE_ATTRIBUTE_COMPRESSED
        offline        = 0x00001000, // FILE_ATTRIBUTE_OFFLINE
        not_indexed    = 0x00002000, // FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
        encrypted      = 0x00004000, // FILE_ATTRIBUTE_ENCRYPTED
    };

    //
    // Metadata for a node: its kind, byte size (0 for a directory), the standard
    // three timestamps, and attribute flags. Timestamps use the surface-wide
    // clock (m::pil::time_point_type). Content is intentionally absent (D14).
    //
    struct file_metadata
    {
        node_kind       m_kind             = node_kind::file;
        std::uint64_t   m_size             = 0; // bytes; always 0 for a directory
        time_point_type m_creation_time    = {};
        time_point_type m_last_write_time  = {};
        time_point_type m_last_access_time = {};
        file_attributes m_attributes       = file_attributes::none;

        constexpr bool
        is_directory() const noexcept
        {
            return m_kind == node_kind::directory;
        }

        constexpr bool
        is_file() const noexcept
        {
            return m_kind == node_kind::file;
        }
    };

    //
    // One child within a directory: its leaf name plus the metadata describing
    // it. Because the namespace is unified (D13), the entry's node-kind (carried
    // inside m_metadata, and mirrored here for convenience) is what distinguishes
    // a subdirectory from a file.
    //
    struct directory_entry
    {
        directory_entry() = default;

        directory_entry(file_name_string_type name, file_metadata metadata):
            m_name(std::move(name)), m_kind(metadata.m_kind), m_metadata(metadata)
        {}

        file_name_string_type m_name;
        // The host's alternate (8.3 short) name for this entry, when one exists
        // (empty otherwise). A path supplied in host syntax may address a child
        // by this alias instead of m_name; consumers that key on m_name resolve
        // such a request by also matching m_short_name. Platforms without an
        // alternate-name concept leave this empty.
        file_name_string_type m_short_name;
        node_kind             m_kind = node_kind::file;
        file_metadata         m_metadata;

        friend void
        swap(directory_entry& l, directory_entry& r) noexcept
        {
            using std::swap;
            swap(l.m_name, r.m_name);
            swap(l.m_short_name, r.m_short_name);
            swap(l.m_kind, r.m_kind);
            swap(l.m_metadata, r.m_metadata);
        }
    };

    //
    // One alternate data stream (ADS) within a file. NTFS files may carry zero or
    // more named streams in addition to their primary (unnamed) data stream. The
    // Win32 surface enumerates streams via FindFirstStreamW / FindNextStreamW;
    // this structure captures what that surface reports.
    //
    // The stream name follows Win32 naming: the unnamed (primary) stream is
    // "::$DATA"; a named stream is ":name:$DATA". The size is the stream's byte
    // extent.
    //
    struct stream_entry
    {
        stream_entry() = default;

        stream_entry(file_name_string_type name, std::uint64_t size):
            m_name(std::move(name)), m_size(size)
        {}

        file_name_string_type m_name; // e.g. "::$DATA" or ":alt:$DATA"
        std::uint64_t         m_size = 0;

        friend void
        swap(stream_entry& l, stream_entry& r) noexcept
        {
            using std::swap;
            swap(l.m_name, r.m_name);
            swap(l.m_size, r.m_size);
        }
    };

    //
    // Access-mode analogue of the registry `sam`. A request expresses the access
    // it needs; a provider maps it to the platform's native rights. The default
    // values mirror the registry surface's "maximum_allowed" convenience so that
    // simple callers need not reason about rights.
    //
    enum class file_access : std::uint32_t
    {
        read       = 0x00000001,
        write      = 0x00000002,
        read_write = read | write,

        default_open   = read,       // default access for opening an existing node
        default_create = read_write, // default access for creating a node
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(file_attributes);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(file_access);

} // namespace m::pil
