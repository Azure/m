// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <stdexcept>
#include <string>

namespace m
{
    namespace utf
    {
        class utf_encoding_error : public std::runtime_error
        {
        public:
            utf_encoding_error(const char* what_arg): std::runtime_error(what_arg) {}
            utf_encoding_error(const std::string& what_arg): std::runtime_error(what_arg) {}
            utf_encoding_error(const utf_encoding_error& other) noexcept: std::runtime_error(other)
            {}

            utf_encoding_error&
                operator=(const utf_encoding_error& other)
            {
                *static_cast<std::runtime_error*>(this) = static_cast<const std::runtime_error&>(other);
                return *this;
            }
        };

        //
        // Indicates when what looked to be a valid encoding hit the end of the stream
        //
        class utf_sequence_truncated_error : public utf_encoding_error
        {
        public:
            utf_sequence_truncated_error(const char* what_arg): utf_encoding_error(what_arg) {}
            utf_sequence_truncated_error(const std::string& what_arg): utf_encoding_error(what_arg) {}
            utf_sequence_truncated_error(const utf_sequence_truncated_error& other) noexcept: utf_encoding_error(other)
            {}

            utf_sequence_truncated_error&
            operator=(const utf_sequence_truncated_error& other)
            {
                *static_cast<utf_encoding_error*>(this) =
                    static_cast<const utf_encoding_error&>(other);
                return *this;
            }
        };

        //
        // Indicates when a stream hit a sequence of one or more bytes
        // which clearly indicated bad UTF-8 data.
        //
        class utf_invalid_encoding_error : public utf_encoding_error
        {
        public:
            utf_invalid_encoding_error(const char* what_arg): utf_encoding_error(what_arg) {}
            utf_invalid_encoding_error(const std::string& what_arg): utf_encoding_error(what_arg)
            {}
            utf_invalid_encoding_error(const utf_invalid_encoding_error& other) noexcept:
                utf_encoding_error(other)
            {}

            utf_invalid_encoding_error&
            operator=(const utf_invalid_encoding_error& other)
            {
                *static_cast<utf_encoding_error*>(this) =
                    static_cast<const utf_encoding_error&>(other);
                return *this;
            }
        };


    }
}