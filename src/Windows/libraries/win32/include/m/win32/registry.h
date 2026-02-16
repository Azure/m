// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <optional>
#include <system_error>
#include <utility>

#include <m/sstring/sstring.h>
#include <m/utility/enum_operations.h>
#include <m/utility/to_underlying.h>
#include <m/win32/event.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32::registry
{
    enum class predefined_key : uintptr_t
    {
        classes_root                = 0x80000000ul, // HKEY_CLASSES_ROOT
        current_user                = 0x80000001ul, // HKEY_CURRENT_USER
        local_machine               = 0x80000002ul, // HKEY_LOCAL_MACHINE
        users                       = 0x80000003ul, // HKEY_USERS
        performance_data            = 0x80000004ul, // HKEY_PERFORMANCE_DATA
        current_config              = 0x80000005ul, // HKEY_CURRENT_CONFIG
        current_user_local_settings = 0x80000007ul, // HKEY_CURRENT_USER_LOCAL_SETTINGS
        performance_text            = 0x80000050ul, // HKEY_PERFORMANCE_TEXT
        performance_nlstext         = 0x80000060ul, // HKEY_PERFORMANCE_NLSTEXT
    };

    std::optional<predefined_key>
    try_map_string_to_predefined_key(m::wsstring str);

    std::optional<predefined_key>
    try_map_hkey_to_predefined_key(HKEY hkey) noexcept;

    bool
    is_predefined_hkey(HKEY hkey) noexcept;

    // In a constant evaluated context, reinterpret_cast<> is not permitted so
    // use this instead.
    //
    constexpr HKEY
    map_predefined_key_to_hkey(predefined_key pk)
    {
        switch (pk)
        {
            using enum predefined_key;

            case classes_root: return HKEY_CLASSES_ROOT;
            case current_user: return HKEY_CURRENT_USER;
            case local_machine: return HKEY_LOCAL_MACHINE;
            case users: return HKEY_USERS;
            case performance_data: return HKEY_PERFORMANCE_DATA;
            case current_config: return HKEY_CURRENT_CONFIG;
            case current_user_local_settings: return HKEY_CURRENT_USER_LOCAL_SETTINGS;
            case performance_text: return HKEY_PERFORMANCE_TEXT;
            case performance_nlstext: return HKEY_PERFORMANCE_NLSTEXT;
            default: M_UNREACHABLE_CODE();
        }
    }

    enum class notify_filters : uint32_t
    {
        change_name       = 0x00000001ul, // REG_NOTIFY_CHANGE_NAME
        change_attributes = 0x00000002ul, // REG_NOTIFY_CHANGE_ATTRIBUTES
        change_last_set   = 0x00000004ul, // REG_NOTIFY_CHANGE_LAST_SET
        change_security   = 0x00000008ul, // REG_NOTIFY_CHANGE_SECURITY
        thread_agnostic   = 0x10000000ul, // REG_NOTIFY_THREAD_AGNOSTIC
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(notify_filters);

    class hkey
    {
    public:
        constexpr hkey() noexcept: m_hkey{} {}

        explicit constexpr hkey(HKEY hkey) noexcept: m_hkey(hkey) {}

        constexpr hkey(hkey&& other) noexcept: m_hkey{}
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
        }

        hkey(hkey const& other) = delete;

        ~hkey() { reset(); }

        constexpr hkey&
        operator=(hkey&& other) noexcept
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
            return *this;
        }

        hkey&
        operator=(hkey const&) = delete;

        constexpr void
        swap(hkey& other) noexcept
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
        }

        void
        reset(HKEY new_hkey = HKEY{})
        {
            auto const old_hkey = std::exchange(m_hkey, new_hkey);
            close_hkey(old_hkey);
        }

        HKEY*
        addressof() noexcept
        {
            return &m_hkey;
        }

        HKEY const*
        addressof() const noexcept
        {
            return &m_hkey;
        }

        constexpr HKEY
        get() const noexcept
        {
            return m_hkey;
        }

        constexpr
        operator HKEY() const noexcept
        {
            return m_hkey;
        }

        constexpr bool
        is_valid() const noexcept
        {
            return closable_hkey(m_hkey);
        }

        constexpr
        operator bool() const noexcept
        {
            return is_valid();
        }

        void
        open(predefined_key pk, PCWSTR sub_key, REGSAM sam_desired);

        template <typename CharT>
        void
        open(predefined_key pk, m::basic_sstring<CharT> const& str, REGSAM sam_desired)
        {
            m::wsstring temp{str};
            open(pk, temp.c_str(), sam_desired);
        }

        [[nodiscard]] std::error_code
        openq(predefined_key pk, PCWSTR sub_key, REGSAM sam_desired);

        template <typename CharT>
        [[nodiscard]] std::error_code
        openq(predefined_key pk, m::basic_sstring<CharT> const& str, REGSAM sam_desired)
        {
            m::wsstring temp{str};
            return openq(pk, temp.c_str(), sam_desired);
        }

        void
        open(hkey const& hkey_base, PCWSTR sub_key, REGSAM sam_desired);

        template <typename CharT>
        void
        open(hkey const& hkey_base, m::basic_sstring<CharT> const& str, REGSAM sam_desired)
        {
            m::wsstring temp{str};
            open(hkey_base, temp.c_str(), sam_desired);
        }

        [[nodiscard]] std::error_code
        openq(hkey const& hkey_base, PCWSTR sub_key, REGSAM sam_desired);

        template <typename CharT>
        [[nodiscard]] std::error_code
        openq(hkey const& hkey_base, m::basic_sstring<CharT> const& str, REGSAM sam_desired)
        {
            m::wsstring temp{str};
            return openq(hkey_base, temp.c_str(), sam_desired);
        }

        void
        notify_change_value(bool           watch_subtree,
                            notify_filters filters,
                            HANDLE         evt,
                            bool           asynchronous = true);

        [[nodiscard]] std::error_code
        notify_change_valueq(bool           watch_subtree,
                             notify_filters filters,
                             HANDLE         evt,
                             bool           asynchronous = true);

        void
        notify_change_value(bool           watch_subtree,
                            notify_filters filters,
                            event const&   evt,
                            bool           asynchronous = true)
        {
            notify_change_value(watch_subtree, filters, evt.get(), asynchronous);
        }

        [[nodiscard]] std::error_code
        notify_change_valueq(bool           watch_subtree,
                             notify_filters filters,
                             event const&   evt,
                             bool           asynchronous = true)
        {
            return notify_change_valueq(watch_subtree, filters, evt.get(), asynchronous);
        }

    private:
        static constexpr bool
        closable_hkey(HKEY h) noexcept
        {
            return h != HKEY{} && h != INVALID_HANDLE_VALUE && !predefined_hkey(h);
        }

        static constexpr bool
        predefined_hkey(HKEY h) noexcept
        {
            return h == HKEY_CLASSES_ROOT || h == HKEY_CURRENT_CONFIG || h == HKEY_CURRENT_USER ||
                   h == HKEY_CURRENT_USER_LOCAL_SETTINGS || h == HKEY_LOCAL_MACHINE ||
                   h == HKEY_PERFORMANCE_DATA || h == HKEY_PERFORMANCE_NLSTEXT ||
                   h == HKEY_PERFORMANCE_TEXT || h == HKEY_USERS;
        }

        static void
        close_hkey(HKEY h)
        {
            if (closable_hkey(h))
                ::RegCloseKey(h);
        }

        HKEY m_hkey{};
    };

} // namespace m::win32::registry
