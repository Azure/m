// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <source_location>
#include <stdexcept>
#include <string>

namespace m
{
    //
    // Try not to introduce state into these exception objects
    //
    // managing their cloning and feeding is complex enough without also
    // trying to manage the state. Note that the copy constructor is
    // noexcept which implies that all state must be refcounted.
    //
    // With these classes not defining any state, we rely on the exception
    // classes defined by the standard library to do "whatever they do"
    // to maintain their adherence to the standard.
    //

    /// <summary>
    /// The `m::runtime_error` class is the specialization of `std::runtime_error`
    /// for runtime errors thrown by the m library.
    ///
    /// Semantics are essentially the same as for `std::runtime_error`:
    ///
    /// https://en.cppreference.com/w/cpp/error/runtime_error.html
    ///
    /// </summary>
    class runtime_error : public std::runtime_error
    {
    public:
        runtime_error(std::string const& what_arg): std::runtime_error(what_arg) {}

        runtime_error(char const* what_arg): std::runtime_error(what_arg) {}

        runtime_error(m::runtime_error const& other) noexcept: std::runtime_error(other) {}

        m::runtime_error&
        operator=(m::runtime_error const& other)
        {
            std::runtime_error::operator=(other);
            return *this;
        }
    };

    /// <summary>
    /// The `m::not_found` class is thrown when a runtime case of an item not
    /// found in a collection when it should be found.
    ///
    /// Note that when working with, for example, classes that represent
    /// collections in C++, this type should not, in general be thrown. During
    /// normal operations, exceptions represent programming errors, not
    /// conditions to run into and then handle after the fact.
    ///
    /// This is why the C++ standard avoids defining such a type.
    ///
    /// We only define it because of the interactions with the
    /// underlying operating system platform. It was possibly to
    /// be added to the m::filesystem namespace but that seemed
    /// too narrow in scope since also the m::pil was going to use it for the
    /// registry support.
    ///
    /// For in-memory structures where atomicity can be managed by the code,
    /// the "not found is a programming error" is a good design principle, but
    /// when dealing with external stores which cannot be managed
    /// transactionally with in-memory data, something must give. No matter
    /// how many times code probes to see if a file already exists before
    /// opening it, it may be deleted before the open is performed.
    ///
    /// Rather than cluttering every code path with preamble code to try to
    /// prevent "file not found" and then still having to deal with it, it is
    /// better to simply deal with file not found.
    ///
    /// As such, having a specific exception to catch makes this simpler. code
    /// within m is responsible for throwing not_found if the item in question
    /// was not present within the collection named.
    ///
    /// A meta-question is whether the same exception is used when the named
    /// collection (directory) is also not found, or in the case of a compound
    /// address like an URL, the node name, or the protocol name. Windows
    /// returns distinct error codes in each of these cases and we will throw
    /// distinct exceptions in them also.
    ///
    /// It's not even clear that "directory not found => file not found", since
    /// "directory not found" may be because a volume failed to mount, which
    /// certainly is a different category of problem from the user or sysadmin
    /// had intentionally deleted or not created the directory.
    ///
    /// </summary>
    class not_found : public m::runtime_error
    {
    public:
        not_found(std::string const& what_arg): m::runtime_error(what_arg) {}
        not_found(char const* what_arg): m::runtime_error(what_arg) {}
        not_found(not_found const& other) noexcept: m::runtime_error(other) {}

        not_found&
        operator=(not_found const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    /// <summary>
    /// The `m::sharing_violation` class is thrown when a resource is accessed
    /// but another entity is preventing access to it.
    ///
    /// The canonical case of this is on Windows where when opening a file in
    /// the filesystem, the caller specifies a mask of read / write / delete
    /// sharing compatibility, and other openers that conflict in their access
    /// mode fail with an explicit error code calling this out.
    /// </summary>
    class sharing_violation : public m::runtime_error
    {
    public:
        sharing_violation(std::string const& what_arg): m::runtime_error(what_arg) {}
        sharing_violation(char const* what_arg): m::runtime_error(what_arg) {}
        sharing_violation(sharing_violation const& other) noexcept: m::runtime_error(other) {}

        sharing_violation&
        operator=(sharing_violation const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class already_exists : public m::runtime_error
    {
    public:
        already_exists(std::string const& what_arg): m::runtime_error(what_arg) {}
        already_exists(char const* what_arg): m::runtime_error(what_arg) {}
        already_exists(already_exists const& other) noexcept: m::runtime_error(other) {}

        already_exists&
        operator=(already_exists const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class invalid_parameter : public m::runtime_error
    {
    public:
        invalid_parameter(std::string const& what_arg): m::runtime_error(what_arg) {}
        invalid_parameter(char const* what_arg): m::runtime_error(what_arg) {}
        invalid_parameter(invalid_parameter const& other) noexcept: m::runtime_error(other) {}

        invalid_parameter&
        operator=(invalid_parameter const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class not_implemented : public m::runtime_error
    {
    public:
        not_implemented(std::string const& what_arg): m::runtime_error(what_arg) {}
        not_implemented(char const* what_arg): m::runtime_error(what_arg) {}
        not_implemented(not_implemented const& other) noexcept: m::runtime_error(other) {}

        not_implemented&
        operator=(not_implemented const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class not_supported : public m::runtime_error
    {
    public:
        not_supported(std::string const& what_arg): m::runtime_error(what_arg) {}
        not_supported(char const* what_arg): m::runtime_error(what_arg) {}
        not_supported(not_supported const& other) noexcept: m::runtime_error(other) {}

        not_supported&
        operator=(not_supported const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class not_empty : public m::runtime_error
    {
    public:
        not_empty(std::string const& what_arg): m::runtime_error(what_arg) {}
        not_empty(char const* what_arg): m::runtime_error(what_arg) {}
        not_empty(not_empty const& other) noexcept: m::runtime_error(other) {}

        not_empty&
        operator=(not_empty const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class precondition_not_met : public m::runtime_error
    {
    public:
        precondition_not_met(std::string const& what_arg): m::runtime_error(what_arg) {}
        precondition_not_met(char const* what_arg): m::runtime_error(what_arg) {}
        precondition_not_met(precondition_not_met const& other) noexcept: m::runtime_error(other) {}

        precondition_not_met&
        operator=(precondition_not_met const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    /// <summary>
    /// The `m::access_denied` class is thrown when the underlying platform
    /// refuses an operation because the caller lacks the required rights. It
    /// mirrors the operating system "access denied" status (for example
    /// ERROR_ACCESS_DENIED on Windows), which is the most common registry
    /// failure a consumer must be prepared to handle.
    /// </summary>
    class access_denied : public m::runtime_error
    {
    public:
        access_denied(std::string const& what_arg): m::runtime_error(what_arg) {}
        access_denied(char const* what_arg): m::runtime_error(what_arg) {}
        access_denied(access_denied const& other) noexcept: m::runtime_error(other) {}

        access_denied&
        operator=(access_denied const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    /// <summary>
    /// The `m::out_of_resources` class is thrown when the underlying platform
    /// cannot complete an operation because a resource has been exhausted (for
    /// example memory, handles, or quota). It mirrors transient operating
    /// system "insufficient resources" statuses.
    /// </summary>
    class out_of_resources : public m::runtime_error
    {
    public:
        out_of_resources(std::string const& what_arg): m::runtime_error(what_arg) {}
        out_of_resources(char const* what_arg): m::runtime_error(what_arg) {}
        out_of_resources(out_of_resources const& other) noexcept: m::runtime_error(other) {}

        out_of_resources&
        operator=(out_of_resources const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

    class assertion_failure
    {
    public:
        constexpr assertion_failure(std::source_location const& sl, std::string_view text) noexcept:
            m_file_name(sl.file_name()), m_line(sl.line()), m_function_name(sl.function_name())
        {
            std::copy_n(text.begin(), (std::min)(text.size(), m_text.size()), m_text.begin());
            m_text[m_text.size() - 1] = 0;
        }

        constexpr assertion_failure(assertion_failure const& other) noexcept:
            m_file_name(other.m_file_name),
            m_line(other.m_line),
            m_function_name(other.m_function_name),
            m_text(other.m_text)
        {}

        constexpr char const*
        file_name() const noexcept
        {
            return m_file_name;
        }

        constexpr std::uint_least32_t
        line() const noexcept
        {
            return m_line;
        }

        constexpr char const*
        function_name() const noexcept
        {
            return m_function_name;
        }

        constexpr std::string_view
        text() const noexcept
        {
            return std::string_view(m_text.data());
        }

    private:
        char const*           m_file_name;
        std::uint_least32_t   m_line;
        char const*           m_function_name;
        std::array<char, 512> m_text;
    };

} // namespace m