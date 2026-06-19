// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/strings/compare.h>
#include <m/utility/locked.h>

#include "../pugihelp.h"
#include <pugixml.hpp>

namespace m::pil::impl::buffered
{
    class platform;
    class registry;
    class filesystem;
    class directory;
    class file;

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = delete;
        key(key_path const& path, time_point_type last_write_time);
        key(std::shared_ptr<ikey> const& underlying_key);
        key(std::shared_ptr<ikey>&& underlying_key) noexcept;
        key(key&& other) noexcept = delete;
        key(key const&)           = delete;
        ~key()                    = default;

        key&
        operator=(key&& other) noexcept = delete;

        key&
        operator=(key const&) = delete;

        void
        swap(key& other) noexcept = delete;

        ikey::create_key_disposition
        create_key(ikey::create_key_flags             flags,
                   pil::key_path const&               path,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags flags,
                   pil::key_path const&   path,
                   sam                    sam_desired) override;

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags             flags,
                    std::optional<pil::key_path> const& name) override;

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                     flags,
                       std::size_t                                    index,
                       std::span<pil::key_path, std::dynamic_extent>& key_names) override;

        ikey::flush_disposition
        flush(ikey::flush_flags flags) override;

        ikey::open_key_disposition
        open_key(ikey::open_key_flags                flags,
                 std::optional<pil::key_path> const& key_name,
                 sam                                 sam_desired,
                 std::shared_ptr<ikey>&              returned_key,
                 std::error_code&                    ec) override;

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags flags,
                              std::size_t&                      subkey_count,
                              std::size_t&                      value_count,
                              std::size_t&                      security_descriptor_size,
                              m::pil::time_point_type&          last_write_time) override;

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags              flags,
                   std::optional<pil::key_path> const& old_key_name,
                   pil::key_path const&                new_key_name) override;

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags      flags,
                     value_name_string_type const& value_name) override;

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(ikey::enumerate_value_names_and_types_flags flags,
                                        std::size_t                                 index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>& values_span) override;

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags    flags,
                       value_name_string_type const& value_name,
                       std::size_t&                  size) override;

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags    flags,
                       value_name_string_type const& value_name,
                       reg_value_type&               type) override;

        ikey::get_value_disposition
        get_value(ikey::get_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type&               type,
                  std::span<std::byte>&         value,
                  std::optional<std::size_t>&   new_bytes_required) override;

        ikey::set_value_disposition
        set_value(ikey::set_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type                type,
                  std::span<std::byte const>    value) override;

        ikey::get_path_disposition
        get_path(ikey::get_path_flags flags, m::pil::key_path& path_out) override;

    protected:
        void
        initialize_overlay();

        void
        initialize_keys_overlay();

        void
        initialize_values_overlay();

        // Read the underlying key's current last_write_time. Used to bracket a
        // whole-key capture so we can detect (and retry) a key that changed
        // underneath us during enumeration. (D4)
        time_point_type
        query_underlying_last_write_time() const;

        // Eagerly materialize every mirrored value's data whole so the overlay
        // is a self-contained snapshot. A value that vanished from the
        // underlying registry between enumeration and load is dropped from the
        // captured set (best-effort, D4).
        void
        load_all_mirrored_values();

        bool
        is_subkey_empty(pil::key_path const& key_name);

        struct key_node
        {
            std::shared_ptr<key> m_key;
            time_point_type      m_last_write_time{(time_point_type::min)()};
            bool                 m_deleted : 1;
            bool                 m_mirrored : 1;
        };

        struct value_node
        {
            reg_value_type                        m_reg_value_type;
            std::optional<std::vector<std::byte>> m_value;
            bool                                  m_deleted;
        };

        void
        unmirror_node(pil::key_path const& key_name, key_node& node);

        void
        unmirror_node(value_name_view_type value_name, value_node& node);

        using key_map_type =
            std::map<m::u16sstring, key_node, m::case_insensitive_less<m::u16sstring>>;

        using value_map_type =
            std::map<m::u16sstring, value_node, m::case_insensitive_less<m::u16sstring>>;

        void
        load_value_if_not_present(value_name_string_type const& value_name, value_node& vnv);

        void
        save_xml(pugi::xml_node& node) const;

        // Populate this (freshly-constructed, materialized, underlying-less) key
        // from a persisted <Key> element: its <Value> and nested <Key> children
        // become fully-materialized overlay entries, except name-only
        // placeholders (mirrored="true"), which are restored as unmaterialized
        // mirrored entries. load_stamp is the single T_load captured for the
        // whole snapshot, used by lazy consistency repair (D5).
        void
        load_children_xml(pugi::xml_node const& key_element, time_point_type load_stamp);

        //
        // data
        //

        mutable std::mutex     m_mutex;
        std::shared_ptr<ikey>  m_underlying_key;
        time_point_type        m_last_write_time;
        // T_load: the timestamp captured when this key was loaded from a
        // snapshot. Used to restamp this key when lazy consistency repair drops
        // a name-only subkey that cannot be materialized (D5). min for keys not
        // loaded from a snapshot.
        time_point_type        m_load_stamp{(time_point_type::min)()};
        key_map_type           m_keys;
        value_map_type         m_values;
        key_path               m_key_path; // only populated for created keys
        std::vector<std::byte> m_security_descriptor;

        friend class registry;
    };

    class registry_monitor :
        public iregistry_monitor,
        public std::enable_shared_from_this<registry_monitor>
    {
    public:
        registry_monitor() = default;
        registry_monitor(std::shared_ptr<iregistry_monitor> const& underlying_registry_monitor);
        registry_monitor(std::shared_ptr<iregistry_monitor>&& underlying_registry_monitor) noexcept;
        registry_monitor(registry_monitor&& other) noexcept = delete;
        registry_monitor(registry_monitor const&)           = delete;
        ~registry_monitor()                                 = default;

        registry_monitor&
        operator=(registry_monitor&& other) noexcept = delete;

        registry_monitor&
        operator=(registry_monitor const&) = delete;

        void
        swap(registry_monitor& other) noexcept = delete;

        register_watch_disposition
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                                path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
                       std::unique_ptr<iregistry_monitor_token>&           returned_ptr) override;

    private:
        std::shared_ptr<iregistry_monitor> m_underlying_registry_monitor;
    };

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;
        registry(std::shared_ptr<iregistry> const& underlying_registry);
        registry(std::shared_ptr<iregistry>&& underlying_registry) noexcept;
        registry(registry&& other) noexcept = delete;
        registry(registry const&)           = delete;
        ~registry()                         = default;

        registry&
        operator=(registry&& other) noexcept = delete;

        registry&
        operator=(registry const&) = delete;

        void
        swap(registry& r) noexcept = delete;

        iregistry::open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags      flags,
                            predefined_key                 pk,
                            sam                            sam_desired,
                            std::shared_ptr<m::pil::ikey>& returned_key) override;

        monitor_disposition
        monitor(monitor_flags                               flags,
                std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor) override;

        // Build a snapshot registry from a persisted <Platform> element. The
        // returned registry has no underlying (live) registry; its predefined
        // keys are fully materialized from the file.
        static std::shared_ptr<registry>
        load_xml(pugi::xml_node const& platform_node);

    protected:
        void
        save_xml(pugi::xml_node& doc_node) const;

        void initialize_monitor(m::locked_t);

        mutable std::mutex                                             m_mutex;
        std::shared_ptr<iregistry>                                     m_underlying_registry;
        std::shared_ptr<iregistry_monitor>                             m_monitor;
        std::map<predefined_key, std::shared_ptr<impl::buffered::key>> m_predefined_keys;

        friend class platform;
    };

    //
    // Ordering for the buffered filesystem's root map. Roots are an open-ended
    // family (D10); two roots are the same iff their kind and (case-insensitive,
    // D12) text agree, so the strict-weak ordering compares kind first, then the
    // root text under ordinal case-insensitive comparison.
    //
    struct file_root_less
    {
        bool
        operator()(file_root const& lhs, file_root const& rhs) const
        {
            if (lhs.kind() != rhs.kind())
                return lhs.kind() < rhs.kind();

            return m::case_insensitive_less<file_root::string_type>{}(lhs.text(), rhs.text());
        }
    };

    //
    // A buffered file node. A file is a leaf in the unified namespace (D13); it
    // carries metadata only (content deferred, D14). The metadata is captured
    // whole when the parent directory is touched (it arrives with the directory
    // enumeration), so a file node is self-contained and never re-reads an
    // underlying provider.
    //
    class file : public ifile, public std::enable_shared_from_this<file>
    {
    public:
        file() = delete;

        // The node carries the captured metadata; there is no underlying handle.
        explicit file(file_metadata const& metadata);

        // Mirrored (unmodified backing) node over a live provider (D16/D17):
        // retains the live underlying handle so whole-file content reads
        // (read_content) resolve to the real backing bytes. Writes are never
        // forwarded, so the shared backing directory is never mutated and the
        // buffered namespace overlay stays the only mutated state.
        file(file_metadata const& metadata, std::shared_ptr<ifile> underlying);

        file(file&& other) noexcept = delete;
        file(file const&)           = delete;
        ~file()                     = default;

        file&
        operator=(file&& other) noexcept = delete;

        file&
        operator=(file const&) = delete;

        void
        swap(file& other) noexcept = delete;

        ifile::query_information_disposition
        query_information(query_information_flags flags, file_metadata& metadata) override;

        // Whole-file content read-through (D16/D17): forwards to the retained
        // backing handle when present; otherwise (sealed snapshot or a
        // created / renamed node with no backing) reports not_supported.
        ifile::read_content_disposition
        read_content(read_content_flags   flags,
                     std::uint64_t        offset,
                     std::span<std::byte> buffer,
                     std::size_t&         bytes_read,
                     std::error_code&     ec) override;

        // Alternate-data-stream enumeration (M-FS-STREAMS-2): forwards to the
        // retained backing handle when present; otherwise reports not_supported.
        ifile::enumerate_streams_disposition
        enumerate_streams(enumerate_streams_flags                       flags,
                          std::size_t                                   starting_index,
                          std::span<stream_entry, std::dynamic_extent>& entries,
                          std::error_code&                              ec) override;

    protected:
        file_metadata          m_metadata;
        std::shared_ptr<ifile> m_underlying;

        friend class directory;
    };

    //
    // A buffered directory node. The container of the unified namespace (D13):
    // each child is exactly one node — a subdirectory or a file. The overlay
    // mirrors the registry key overlay: a child-entry map keyed by an ordinal
    // case-insensitive sort key with original case preserved (D12), holding
    // tombstones (deleted) and mirrored-but-unmaterialized placeholders. The
    // whole node is captured on touch with last_write_time bracketing and a
    // bounded retry on a torn read (D3, D4 analogues; non-recursive).
    //
    class directory : public idirectory, public std::enable_shared_from_this<directory>
    {
    public:
        directory() = delete;

        // Snapshot / created node: own metadata only, no underlying directory.
        directory(file_metadata const& metadata, std::nullptr_t);

        // Capture node: snapshot the underlying directory whole on touch.
        explicit directory(std::shared_ptr<idirectory> const& underlying_directory);

        directory(directory&& other) noexcept = delete;
        directory(directory const&)           = delete;
        ~directory()                          = default;

        directory&
        operator=(directory&& other) noexcept = delete;

        directory&
        operator=(directory const&) = delete;

        void
        swap(directory& other) noexcept = delete;

        idirectory::create_directory_disposition
        create_directory(create_directory_flags       flags,
                         file_path const&             path,
                         file_access                  access,
                         std::shared_ptr<idirectory>& returned_directory) override;

        idirectory::create_file_disposition
        create_file(create_file_flags       flags,
                    file_path const&        path,
                    file_access             access,
                    std::shared_ptr<ifile>& returned_file) override;

        idirectory::open_directory_disposition
        open_directory(open_directory_flags         flags,
                       file_path const&             path,
                       file_access                  access,
                       std::shared_ptr<idirectory>& returned_directory,
                       std::error_code&             ec) override;

        idirectory::open_file_disposition
        open_file(open_file_flags         flags,
                  file_path const&        path,
                  file_access             access,
                  std::shared_ptr<ifile>& returned_file,
                  std::error_code&        ec) override;

        idirectory::remove_entry_disposition
        remove_entry(remove_entry_flags flags, file_path const& name) override;

        idirectory::delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::optional<file_path> const& name) override;

        idirectory::rename_entry_disposition
        rename_entry(rename_entry_flags flags,
                     file_path const&   old_path,
                     file_path const&   new_path) override;

        idirectory::enumerate_entries_disposition
        enumerate_entries(enumerate_entries_flags                          flags,
                          std::size_t                                      starting_index,
                          std::span<directory_entry, std::dynamic_extent>& entries) override;

        idirectory::query_information_disposition
        query_information(query_information_flags flags, file_metadata& metadata) override;

    protected:
        // One child of this directory in the unified namespace (D13). The kind
        // distinguishes a subdirectory from a file; exactly one of m_directory /
        // m_file is non-null once materialized. A mirrored placeholder has both
        // null until first touched; a tombstone (m_deleted) has both null.
        struct entry_node
        {
            node_kind                  m_kind{node_kind::file};
            std::shared_ptr<directory> m_directory;
            std::shared_ptr<file>      m_file;
            file_metadata              m_metadata{};
            bool                       m_deleted : 1;
            bool                       m_mirrored : 1;
            // The host's alternate (8.3 short) name for this child, when one
            // exists (empty otherwise). Captured from the underlying enumeration
            // (D14) and persisted (D17) so a path component supplied as the short
            // alias resolves to this entry even for a sealed snapshot that has no
            // live underlying to consult.
            m::u16sstring              m_short_name;
        };

        using entry_map_type =
            std::map<m::u16sstring, entry_node, m::case_insensitive_less<m::u16sstring>>;

        // Capture the whole directory at materialization ("touch"): enumerate
        // the underlying child entries (name + kind + metadata) and snapshot
        // this directory's own metadata, bracketed by last_write_time with a
        // bounded retry on a torn read (D3, D4). Non-recursive: children are
        // mirrored placeholders, materialized on first touch.
        void
        initialize_overlay();

        void
        initialize_children_overlay();

        // Read the underlying directory's current metadata. Used both to capture
        // this node's metadata and to bracket the capture for torn-read retry.
        file_metadata
        query_underlying_metadata() const;

        // Materialize a mirrored placeholder child into a live buffered node by
        // opening it through the underlying directory. In a sealed snapshot (no
        // underlying) a mirrored placeholder cannot be materialized; lazy
        // consistency repair (D5) drops it and restamps this directory.
        std::shared_ptr<directory>
        materialize_subdirectory(m::u16sstring const& name, entry_node& node, file_access access);

        std::shared_ptr<file>
        materialize_file(m::u16sstring const& name, entry_node& node, file_access access);

        // True when no live (non-tombstoned) child remains. Locks m_mutex.
        bool
        is_empty() const;

        // Detach a live child by leaf name for a rename/move: materialize it so
        // the moved node is self-contained, tombstone the old slot, and return
        // the node. Throws m::not_found when the name names no live child.
        entry_node
        extract_entry(m::u16sstring const& leaf);

        // Place a child node under leaf name for a rename/move. A live child
        // already at that name is a conflict (m::already_exists); a tombstone is
        // overwritten.
        void
        insert_entry(m::u16sstring const& leaf, entry_node node);

        void
        save_xml(pugi::xml_node& parent) const;

        // Populate this freshly-constructed (underlying-less) directory from a
        // persisted <Directory> element. load_stamp is the single T_load for the
        // whole snapshot, used by lazy consistency repair (D5).
        void
        load_children_xml(pugi::xml_node const& directory_element, time_point_type load_stamp);

        mutable std::mutex          m_mutex;
        std::shared_ptr<idirectory> m_underlying_directory;
        file_metadata               m_metadata;
        // T_load: timestamp captured when this directory was loaded from a
        // snapshot; min for directories not loaded from a snapshot (D5).
        time_point_type             m_load_stamp{(time_point_type::min)()};
        entry_map_type              m_entries;

        friend class filesystem;
    };

    class filesystem_monitor :
        public ifilesystem_monitor,
        public std::enable_shared_from_this<filesystem_monitor>
    {
    public:
        filesystem_monitor() = default;
        filesystem_monitor(std::shared_ptr<ifilesystem_monitor> const& underlying_filesystem_monitor);
        filesystem_monitor(
            std::shared_ptr<ifilesystem_monitor>&& underlying_filesystem_monitor) noexcept;
        filesystem_monitor(filesystem_monitor&& other) noexcept = delete;
        filesystem_monitor(filesystem_monitor const&)           = delete;
        ~filesystem_monitor()                                   = default;

        filesystem_monitor&
        operator=(filesystem_monitor&& other) noexcept = delete;

        filesystem_monitor&
        operator=(filesystem_monitor const&) = delete;

        void
        swap(filesystem_monitor& other) noexcept = delete;

        register_watch_disposition
        register_watch(
            register_watch_flags                                  flags,
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr,
            std::unique_ptr<ifilesystem_monitor_token>&           returned_ptr) override;

    private:
        std::shared_ptr<ifilesystem_monitor> m_underlying_filesystem_monitor;
    };

    //
    // The buffered filesystem entry point. Opening a root (D10) yields the
    // buffered directory anchoring its namespace; the directory is cached so
    // repeated opens of the same root share one overlay.
    //
    class filesystem : public ifilesystem, public std::enable_shared_from_this<filesystem>
    {
    public:
        filesystem() = delete;

        explicit filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem);

        filesystem(filesystem&& other) noexcept = delete;
        filesystem(filesystem const&)           = delete;
        ~filesystem()                           = default;

        filesystem&
        operator=(filesystem&& other) noexcept = delete;

        filesystem&
        operator=(filesystem const&) = delete;

        void
        swap(filesystem& other) noexcept = delete;

        ifilesystem::open_root_disposition
        open_root(open_root_flags              flags,
                  file_root const&             root,
                  file_access                  access,
                  std::shared_ptr<idirectory>& returned_directory) override;

        ifilesystem::monitor_disposition
        monitor(monitor_flags                                 flags,
                std::shared_ptr<m::pil::ifilesystem_monitor>& returned_filesystem_monitor) override;

        // Build a snapshot filesystem from a persisted <Platform> element. The
        // returned filesystem has no underlying (live) filesystem; its roots are
        // fully materialized from the file.
        static std::shared_ptr<filesystem>
        load_xml(pugi::xml_node const& platform_node);

    protected:
        void
        save_xml(pugi::xml_node& doc_node) const;

        void initialize_monitor(m::locked_t);

        mutable std::mutex                                       m_mutex;
        std::shared_ptr<ifilesystem>                            m_underlying_filesystem;
        std::shared_ptr<ifilesystem_monitor>                    m_monitor;
        std::map<file_root, std::shared_ptr<directory>, file_root_less> m_roots;

        friend class platform;
    };

    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;
        platform(std::shared_ptr<iplatform> const& underlying_platform);
        platform(std::shared_ptr<iplatform>&& underlying_platform);

        // Snapshot constructor: no underlying (live) platform. Reads and writes
        // operate purely against the supplied loaded registry and filesystem
        // (mode (c)). Either snapshot facet may be null when only one surface was
        // persisted.
        explicit platform(std::shared_ptr<registry>   snapshot_registry,
                          std::shared_ptr<filesystem> snapshot_filesystem = {});

        platform(platform&& other) noexcept = delete;
        platform(platform const&)           = delete;
        ~platform()                         = default;

        platform&
        operator=(platform&& other) noexcept = delete;

        platform&
        operator=(platform const&) = delete;

        void
        swap(platform& other) noexcept = delete;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

        get_filesystem_disposition
        get_filesystem(get_filesystem_flags          flags,
                       std::shared_ptr<ifilesystem>& returned_filesystem) override;

        get_webcore_disposition
        get_webcore(get_webcore_flags          flags,
                    std::shared_ptr<iwebcore>& returned_webcore) override;

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override;

        // D6: decorators forward the diagnostic-log request down the stack so a
        // logging tap placed beneath this layer remains reachable from the top.
        // The buffered layer records no diagnostic trace of its own.
        save_disposition
        save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element) override;

    protected:
        mutable std::mutex          m_mutex;
        std::shared_ptr<iplatform>  m_underlying_platform;
        std::shared_ptr<registry>   m_registry;
        std::shared_ptr<filesystem> m_filesystem;
    };

    // Build a snapshot platform from a previously persisted XML file. The
    // returned platform has no underlying (live) platform, so reads and writes
    // operate purely against the loaded state (mode (c)).
    std::shared_ptr<iplatform>
    create_platform_from_persisted_xml(std::filesystem::path const& p);

} // namespace m::pil::impl::buffered
