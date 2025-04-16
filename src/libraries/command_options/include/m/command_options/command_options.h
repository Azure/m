// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/cast/try_cast.h>
#include <m/filesystem/filesystem.h>
#include <m/strings/convert.h>
#include <m/strings/static_string.h>

namespace m
{
    namespace command_options
    {
        enum class option_match_type
        {
            not_a_match,    // not a match at all
            positive_match, // matches
            negated_match,  // matches if you put "no" in front of the option name
        };

        template <typename CharT>
        static inline constexpr std::array<CharT, 3> negation_prefix{'n', 'o', '-'};

        template <typename CharT>
        static inline constexpr std::array<CharT, 2> dashdash{'-', '-'};

        template <typename CharT>
        class option;

        template <typename CharT>
        class parameter;

        template <typename CharT>
        class verb;

        template <typename CharT>
        class command_verb_set;

        template <typename CharT>
        std::optional<std::size_t>
        starts_with_dashdash(std::basic_string_view<CharT, std::char_traits<CharT>> const& str)
        {
            if (str.size() >= dashdash<CharT>.size())
            {
                for (size_t i = 0; i < dashdash<CharT>.size(); i++)
                {
                    if (str[i] != dashdash<CharT>[i])
                        return std::nullopt;
                }

                return str.size() - dashdash<CharT>.size();
            }

            return std::nullopt;
        }

        template <typename CharT>
        class option
        {
        public:
            using char_t        = CharT;
            using char_traits_t = std::char_traits<char_t>;
            using string_view_t = std::basic_string_view<char_t, char_traits_t>;
            using string_t      = std::basic_string<char_t, char_traits_t>;

            virtual ~option() = default;

            string_t
            name() const
            {
                return m_name;
            }

            bool
            negatable() const
            {
                return m_negatable;
            }

            template <typename Iter>
            Iter
            usage(Iter it, std::size_t description_column)
            {
                //
                // Assume on a fresh line
                //

                auto fourspaces = {' ', ' ', ' ', ' '};

                auto twodashes = {'-', '-'};

                // We could just always write the "    --" but perhaps we
                // want to enable single dash aliases in the future so we'll
                // keep the logic / writes separated.

                std::size_t col{};

                it = std::ranges::transform(fourspaces, it, [](auto ch) -> char_t {
                         return static_cast<char_t>(ch);
                     }).out;
                col += fourspaces.size();

                //
                // See if we can unify these later
                //

                it = std::ranges::copy(twodashes, it).out;
                col += twodashes.size();

                if (m_negatable)
                {
                    auto bracketnobracket = {'[', 'n', 'o', '-', ']'};
                    it                    = std::ranges::copy(bracketnobracket, it).out;
                    col += bracketnobracket.size();
                }

                auto option_name_view = string_view_t(m_name).substr(dashdash<char_t>.size());
                it                    = std::ranges::copy(option_name_view, it).out;
                col += option_name_view.size();

                if (col >= description_column)
                {
                    *it = '\n';
                    ++it;
                    col = 0;
                }

                while (col < description_column)
                {
                    *it = ' ';
                    ++it;
                    ++col;
                }

                auto TBD = {
                    'T', 'o', ' ', 'b', 'e', ' ', 'd', 'e', 't', 'e', 'r', 'm', 'i', 'n', 'e', 'd'};

                it = std::ranges::copy(TBD, it).out;

                *it = '\n';
                ++it;

                return it;
            }

            bool
            defaulted() const
            {
                return m_defaulted;
            }

            bool
            process_option(int& index, int argc, char_t const* argv[])
            {
                bool processed = false;

                if (index >= argc)
                    throw std::runtime_error("internal error - past end of command line arguments");

                auto const match_type = try_match(argv[index]);

                if (match_type == option_match_type::not_a_match)
                    return false;

                m_match_type = match_type;

                ++index;

                return do_process_option(index, argc, argv);
            }

        protected:
            option(string_view_t name, bool negatable):
                m_name(name),
                m_negatable(negatable),
                m_defaulted(true),
                m_match_type(option_match_type::not_a_match)
            {
                if (m_negatable)
                {
                    m_negated_name.reserve(m_name.size() + 2);
                    m_negated_name.push_back('n');
                    m_negated_name.push_back('o');
                    m_negated_name.append(m_name);
                }
            }

            option_match_type
            try_match(string_view_t option)
            {
                if (auto x = starts_with_dashdash(option); x)
                {
                    auto rest = option.substr(2);

                    if (m_name.compare(rest) == 0)
                        return option_match_type::positive_match;
                    else if (m_negated_name.compare(rest) == 0)
                        return option_match_type::negated_match;
                }

                return option_match_type::not_a_match;
            }

            virtual bool
            do_process_option(int& index, int argc, char_t const* argv[]) = 0;

            string_t          m_name;
            string_t          m_negated_name;
            bool              m_negatable;
            bool              m_defaulted;
            option_match_type m_match_type;
        };

        template <typename CharT>
        class boolean_option : public option<CharT>
        {
        public:
            using char_t        = CharT;
            using char_traits_t = std::char_traits<char_t>;
            using string_view_t = std::basic_string_view<char_t, char_traits_t>;
            using string_t      = std::basic_string<char_t, char_traits_t>;

            boolean_option(string_view_t name,
                           bool&         value,
                           bool          negatable     = true,
                           bool          default_value = false):
                option<char_t>(name, negatable), m_default_value(default_value), m_value(value)
            {
                m_value = default_value;
            }

        protected:
            bool
            do_process_option(int& index, int argc, char_t const* argv[]) override
            {
                option<char_t>::m_defaulted = false;

                // The value itself tracks the "positivity" of the option
                if (option<char_t>::m_match_type == option_match_type::positive_match)
                    m_value = true;
                else
                    m_value = false;

                return true;
            }

            bool  m_default_value;
            bool& m_value;
        };

        template <typename CharT>
        class string_option : public option<CharT>
        {
        public:
            using char_t        = CharT;
            using char_traits_t = std::char_traits<char_t>;
            using string_view_t = std::basic_string_view<char_t, char_traits_t>;
            using string_t      = std::basic_string<char_t, char_traits_t>;

            string_option(string_view_t name,
                          string_t&     value,
                          string_view_t default_value = string_view_t{},
                          bool          negatable     = false):
                option<char_t>(name, negatable), m_default_value(default_value), m_value(value)
            {
                m_value = default_value;
            }

        protected:
            bool
            do_process_option(int& index, int argc, char_t const* argv[]) override
            {
                option<char_t>::m_defaulted = false;

                if (index >= argc)
                    throw std::runtime_error("too few command line arguments");

                m_value = argv[index];
                index++;
                return true;
            }

            string_t  m_default_value;
            string_t& m_value;
        };

        template <typename CharT>
        class path_option : public option<CharT>
        {
        public:
            using char_t        = CharT;
            using char_traits_t = std::char_traits<char_t>;
            using string_view_t = std::basic_string_view<char_t, char_traits_t>;
            using string_t      = std::basic_string<char_t, char_traits_t>;

            path_option(string_view_t          name,
                        std::filesystem::path& value,
                        string_view_t          default_value = string_view_t{},
                        bool                   negatable     = false):
                option<char_t>(name, negatable), m_default_value(default_value), m_value(value)
            {
                m_value = default_value;
            }

        protected:
            bool
            do_process_option(int& index, int argc, char_t const* argv[]) override
            {
                option<char_t>::m_defaulted = false;

                if (index >= argc)
                    throw std::runtime_error("too few command line arguments");

                m_value = m::filesystem::make_path(argv[index]);
                index++;
                return true;
            }

            std::filesystem::path  m_default_value;
            std::filesystem::path& m_value;
        };

        template <typename CharT>
        class parameter
        {
        public:
            using char_t        = CharT;
            using string_view_t = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using string_t      = std::basic_string<char_t, std::char_traits<char_t>>;
            using option_t      = option<char_t>;

            parameter()                 = delete;
            parameter(parameter const&) = delete;
            parameter(parameter&&)      = delete;
            void
            operator=(parameter const&) = delete;

            virtual ~parameter() = default;

            string_t
            name() const
            {
                return m_name;
            }

            bool
            required() const
            {
                return m_required;
            }

            template <typename Iter>
            Iter
            usage(Iter it)
            {
                if (!m_required)
                {
                    *it = '[';
                    ++it;
                }

                *it = '<';
                ++it;

                it = std::ranges::copy(m_name, it).out;

                *it = '>';
                ++it;

                if (!m_required)
                {
                    *it = ']';
                    ++it;
                }

                *it = ' ';
                ++it;

                return it;
            }

            bool
            defaulted() const
            {
                return m_defaulted;
            }

            void
            process_parameter(int& index, int argc, char_t const* argv[])
            {
                bool processed = false;

                if (index >= argc && m_required)
                    throw std::runtime_error("too few arguments");

                if (index < argc)
                {
                    auto const argument_view = string_view_t(argv[index]);

                    if (starts_with_dashdash(argument_view))
                    {
                        if (m_required)
                            throw std::runtime_error("missing required parameter");
                    }

                    do_process_parameter(index, argc, argv);
                }
            }

        protected:
            parameter(string_view_t name, bool required): m_name(name), m_required(required) {}

            virtual void
            do_process_parameter(int& index, int argc, char_t const* argv[]) = 0;

            string_t m_name;
            bool     m_defaulted;
            bool     m_required;

            template <typename>
            class verb;
        };

        template <typename CharT>
        class path_parameter : public parameter<CharT>
        {
        public:
            using char_t        = CharT;
            using string_view_t = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using string_t      = std::basic_string<char_t, std::char_traits<char_t>>;

            path_parameter(string_view_t          name,
                           std::filesystem::path& value,
                           bool                   required      = true,
                           string_view_t          default_value = string_view_t{}):
                parameter<CharT>(name, required), m_value(value), m_default_value(default_value)
            {
                m_value = m::filesystem::make_path(m_default_value);
            }

            path_parameter()                      = delete;
            path_parameter(path_parameter const&) = delete;
            path_parameter(path_parameter&&)      = delete;
            void
            operator=(path_parameter const&) = delete;

        protected:
            std::filesystem::path& m_value;
            string_t               m_default_value;

            void
            do_process_parameter(int& index, int argc, char_t const* argv[]) override
            {
                if (index >= argc)
                {
                    if (parameter<char_t>::m_required)
                        throw std::runtime_error("missing required parameter");
                }
                else
                {
                    m_value = m::filesystem::make_path(argv[index]);
                    index++;
                    parameter<char_t>::m_defaulted = false;
                }
            }
        };

        template <typename CharT>
        class verb
        {
        public:
            using char_t        = CharT;
            using string_view_t = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using string_t      = std::basic_string<char_t, std::char_traits<char_t>>;
            using parameter_t   = parameter<char_t>;
            using option_t      = option<char_t>;

            verb(string_view_t name): m_name(name) {}

            verb()            = delete;
            verb(verb const&) = delete;
            verb(verb&&)      = delete;
            void
            operator=(verb const&) = delete;
            void
            operator=(verb&&) = delete;

            ~verb() = default;

            string_t
            name() const
            {
                return m_name;
            }

            template <typename Iter>
            Iter
            usage(Iter it)
            {
                it = std::ranges::copy(m_name, it).out;

                *it = ' ';
                ++it;

                // number of closing brackets to write at end of parameter
                // list for optional parameters begun
                std::size_t optional_count = 0;

                for (auto&& p: m_parameters)
                {
                    if (!p->required())
                    {
                        *it = '[';
                        ++it;

                        optional_count++;
                    }

                    *it = '<';
                    ++it;

                    it = std::ranges::copy(p->name(), it).out;

                    *it = '>';
                    ++it;

                    *it = ' ';
                    ++it;
                }

                while (optional_count > 0)
                {
                    *it = ']';
                    ++it;
                    optional_count--;
                }

                auto sometext = {
                    ' ', '[', '<', 'O', 'p', 't', 'i', 'o', 'n', 's', '>', ']', '\n', '\n'};

                it = std::ranges::copy(sometext, it).out;

                for (auto&& p: m_parameters)
                    it = p->usage(it);

                *it = '\n';
                ++it;

                for (auto&& o: m_options)
                    it = o->usage(it, 35);

                *it = '\n';
                ++it;

                return it;
            }

            path_parameter<char_t>&
            add_path_parameter(string_view_t name, std::filesystem::path& path)
            {
                auto p1 = std::make_unique<path_parameter<char_t>>(name, path);

                auto index = m_parameters.size();
                m_parameters.push_back(nullptr);

                // What was the size is now the index
                m_parameters[index].reset(p1.release());
                return *m::try_cast<path_parameter<char_t>*>(m_parameters[index].get());
            }

#if 0
            option_t&
            add_option(string_view_t name)
            {
                return m_options.emplace_back(option_t(name));
            }
#endif

            bool
            try_process_command_line(int& index, int argc, char_t const* argv[])
            {
                if (index >= argc)
                    throw std::runtime_error("argument count insufficient");

                if (!verb_matches(argv[index]))
                    return false;

                index++;

                //
                // This is kind of messed up; sort it out at some point.
                // Assumes that all parameters occur then all options
                // (switches).
                //

                for (auto&& p: m_parameters)
                    p->process_parameter(index, argc, argv);

                while (index < argc)
                {
                    bool option_found = false;

                    for (auto&& e: m_options)
                    {
                        if (e->process_option(index, argc, argv))
                        {
                            option_found = true;
                            break;
                        }
                    }

                    // Need a proper way to report errors to user??
                    if (!option_found)
                        throw std::runtime_error("invalid command line switch");
                }

                return true;
            }

        protected:
            bool
            verb_matches(string_view_t arg)
            {
                return m_name.compare(arg) == 0;
            }

            string_t                                  m_name;
            std::vector<std::unique_ptr<option_t>>    m_options;
            std::vector<std::unique_ptr<parameter_t>> m_parameters;
        };

        template <typename CharT>
        class command_verb_set
        {
        public:
            using char_t        = CharT;
            using string_view_t = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using string_t      = std::basic_string<char_t, std::char_traits<char_t>>;
            using option_t      = option<char_t>;
            using verb_t        = verb<char_t>;

            command_verb_set() {}
            command_verb_set(command_verb_set const&) = delete;
            command_verb_set(command_verb_set&&)      = delete;

            void
            operator=(command_verb_set const&) = delete;

            verb_t&
            add_verb(string_view_t name)
            {
                auto p1 = std::make_unique<verb_t>(name);

                auto index = m_verbs.size();
                m_verbs.push_back(nullptr);

                // What was the size is now the index
                m_verbs[index].reset(p1.release());
                return *(m_verbs[index].get());
            }

            bool
            process_command_line(int& index, int argc, char_t const* argv[])
            {
                bool matched = false;

                for (auto&& v: m_verbs)
                {
                    if (v->try_process_command_line(index, argc, argv))
                    {
                        matched = true;
                        break;
                    }
                }

                return matched;
            }

            bool
            process_command_line(int argc, char_t const* argv[])
            {
                int index = 1;
                if (!process_command_line(index, argc, argv))
                    throw std::runtime_error("No matching command");

                return true;
            }

            string_t
            usage(std::filesystem::path program)
            {
                auto filename     = program.filename();
                auto filename_str = filename.c_str();
                auto filename2    = m::to_string(filename_str);

                string_t buffer;
                auto     it = std::back_inserter(buffer);

                // clang-format off
                if constexpr (std::is_same_v<CharT, char>)
                {
                    it = std::format_to(it,
                        "Usage:\n"
                        "\n"
                        "    {} {{ ", filename2);

                    for (auto&& v : m_verbs)
                    {
                        it = std::format_to(it, "[{}] ", v->name());
                    }

                    it = std::format_to(it,
                        "}}\n"
                        "\n"
                        "Where:\n"
                        "\n");

                    for (auto&& v : m_verbs)
                    {
                        it = v->usage(it);
                    }
                }

                return buffer;
            }

        protected:
            std::vector<std::unique_ptr<verb_t>> m_verbs;
        };

        template <typename CharT>
        void
        parse(int argc, CharT const* args[])
        {
            //
            // We assume that this is the full argument set from main(), so
            // argv[0] is the program itself.
            //
        }
    } // namespace command_options
} // namespace m