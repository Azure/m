// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
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
        not_implemented(invalid_parameter const& other) noexcept: m::runtime_error(other) {}

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
        not_supported(invalid_parameter const& other) noexcept: m::runtime_error(other) {}

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
        not_empty(invalid_parameter const& other) noexcept: m::runtime_error(other) {}

        not_empty&
        operator=(not_empty const& other)
        {
            m::runtime_error::operator=(other);
            return *this;
        }
    };

} // namespace m