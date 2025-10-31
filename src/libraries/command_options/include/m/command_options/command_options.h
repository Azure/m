// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
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
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <m/cast/to.h>
#include <m/cast/try_cast.h>
#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/filesystem/filesystem.h>
#include <m/sstring/sstring.h>
#include <m/string_buffer/string_buffer.h>
#include <m/string_buffer/try_cast_basic_sstring.h>
#include <m/strings/convert.h>
#include <m/strings/static_string.h>

namespace m
{
    using namespace std::string_literals;
    using namespace std::string_view_literals;

    namespace command_options
    {
        enum class option_match_type
        {
            not_a_match,    // not a match at all
            positive_match, // matches
            negated_match,  // matches if you put "no" in front of the option name
        };

        template <typename CharT>
        static inline constexpr std::array<CharT, 3> negation_prefix_array = {'n', 'o', '-'};

        template <typename CharT>
        static inline constexpr std::basic_string_view<CharT> negation_prefix{
            negation_prefix_array<CharT>.data(),
            negation_prefix_array<CharT>.size()};

        template <typename CharT>
        static inline constexpr std::array<CharT, 2> dashdash_array = {'-', '-'};

        template <typename CharT>
        static inline constexpr std::basic_string_view<CharT> dashdash{
            dashdash_array<CharT>.data(),
            dashdash_array<CharT>.size()};

        template <typename CharT>
        using value = std::variant<bool, basic_sstring<CharT>, std::filesystem::path>;

        template <typename CharT, typename T>
        concept is_value_v = std::is_same_v<T, bool> || std::is_same_v<T, basic_sstring<CharT>> ||
                             std::is_same_v<T, std::filesystem::path>;

        template <typename CharT>
        class option;

        template <typename CharT>
        class parameter;

        template <typename CharT>
        class path_parameter;

        template <typename CharT>
        class verb;

        template <typename CharT>
        class command_verb_set;

        template <typename CharT>
        class parsed_option;

        template <typename CharT>
        class parsed_parameter;

        template <typename CharT>
        class parsed_command;

        template <typename CharT>
        using verb_action = std::function<void(m::not_null<parsed_command<CharT>*>)>;

        template <typename CharT>
        std::optional<std::size_t>
        starts_with_dashdash(std::basic_string_view<CharT, std::char_traits<CharT>> const& str)
        {
            if (str.starts_with(dashdash<CharT>))
                return str.size() - dashdash<CharT>.size();

            return std::nullopt;
        }

        template <typename CharT>
        class parsed_parameter
        {
            using char_t             = CharT;
            using verb_t             = verb<char_t>;
            using parameter_t        = parameter<char_t>;
            using parsed_parameter_t = parsed_parameter<char_t>;
            using parsed_option_t    = parsed_option<char_t>;
            using parsed_command_t   = parsed_command<char_t>;
            using value_t            = value<char_t>;
            using sstring_t          = basic_sstring<char_t>;

        public:
            parsed_parameter() = delete;
            parsed_parameter(m::not_null<parameter_t*> parameter, value_t value):
                m_parameter(parameter), m_value(std::move(value))
            {}
            parsed_parameter(parsed_parameter const&) = delete;

            parsed_parameter&
            operator=(parsed_parameter const&) = delete;

            sstring_t
            name() const
            {
                return m_parameter->name();
            }

            value_t
            value() const
            {
                return m_value;
            }

        protected:
            m::not_null<parameter_t*> m_parameter;
            value_t                   m_value;
        };

        template <typename CharT>
        class parsed_option
        {
            using char_t             = CharT;
            using sstring_t          = basic_sstring<char_t>;
            using verb_t             = verb<char_t>;
            using option_t           = option<char_t>;
            using parsed_parameter_t = parsed_parameter<char_t>;
            using parsed_option_t    = parsed_option<char_t>;
            using value_t            = value<char_t>;

        public:
            parsed_option() = delete;
            parsed_option(m::not_null<option_t*> option, value_t value):
                m_option(option), m_value(std::move(value))
            {}
            parsed_option(parsed_option const&) = delete;

            parsed_option&
            operator=(parsed_option const&) = delete;

            sstring_t
            name() const
            {
                return m_option->name();
            }

            value_t
            value() const
            {
                return m_value;
            }

        protected:
            m::not_null<option_t*> m_option;
            value_t                m_value;
        };

        template <typename CharT>
        class parsed_command
        {
            using char_t             = CharT;
            using sstring_t          = basic_sstring<char_t>;
            using verb_t             = verb<char_t>;
            using option_t           = option<char_t>;
            using parameter_t        = parameter<char_t>;
            using parsed_parameter_t = parsed_parameter<char_t>;
            using parsed_option_t    = parsed_option<char_t>;

        public:
            parsed_command() = default;

            constexpr verb_t*
            verb() const noexcept
            {
                return m_verb;
            }

            template <typename T, typename StringishT>
                requires(is_value_v<char_t, T>)
            T
            get_parameter(StringishT const& name) const
            {
                auto const pp = get_parsed_parameter(name);
                return std::get<T>(pp->value());
            }

            template <typename T, typename StringishT>
                requires(is_value_v<char_t, T>)
            T
            get_option(StringishT const& name) const
            {
                auto const po = get_parsed_option(name);
                return std::get<T>(po->value());
            }

            template <typename StringishT>
            parsed_parameter_t*
            try_get_parsed_parameter(StringishT const& name) const
            {
                auto const it = m_parameter_map.find(name);
                if (it == m_parameter_map.end())
                    return nullptr;

                return it->second;
            }

            template <typename StringishT>
            m::not_null<parsed_parameter_t*>
            get_parsed_parameter(StringishT const& name) const
            {
                auto const it = m_parameter_map.find(name);
                if (it == m_parameter_map.end())
                {
                    throw m::not_found("Parameter not present in parsed command");
                }

                return it->second;
            }

            template <typename StringishT>
            parsed_option_t*
            try_get_parsed_option(StringishT const& name) const
            {
                auto const it = m_option_map.find(name);
                if (it == m_option_map.end())
                    return nullptr;

                return it->second;
            }

            template <typename StringishT>
            m::not_null<parsed_option_t*>
            get_parsed_option(StringishT const& name) const
            {
                auto const it = m_option_map.find(name);
                if (it == m_option_map.end())
                {
                    throw m::not_found("Option not present in parsed command");
                }

                return it->second;
            }

        protected:
            constexpr void
            verb(verb_t* v) noexcept
            {
                m_verb = v;
            }

            void
            add_parsed_parameter(std::unique_ptr<parsed_parameter_t>&& pp)
            {
                M_INTERNAL_ERROR_CHECK(pp.get() != nullptr);

                // I'm sure there's a more elegant way to express this but I want to ensure that
                // the lifetime is transferred correctly and obviously.
                std::unique_ptr<parsed_parameter_t> localpp;
                using std::swap;
                swap(localpp, pp);

                auto const paramcount = m_parameters.size();
                m_parameters.resize(paramcount + 1);
                swap(localpp, m_parameters[paramcount]);

                m_parameter_map.emplace(m_parameters[paramcount]->name(),
                                        m_parameters[paramcount].get());

                M_INTERNAL_ERROR_CHECK(localpp.get() == nullptr);
            }

            void
            add_parsed_option(std::unique_ptr<parsed_option_t>&& po)
            {
                M_INTERNAL_ERROR_CHECK(po.get() != nullptr);

                // I'm sure there's a more elegant way to express this but I want to ensure that
                // the lifetime is transferred correctly and obviously.
                std::unique_ptr<parsed_option_t> localpo;
                using std::swap;
                swap(localpo, po);

                auto const optcount = m_options.size();
                m_options.resize(optcount + 1);
                swap(localpo, m_options[optcount]);

                m_option_map.emplace(m_options[optcount]->name(), m_options[optcount].get());

                M_INTERNAL_ERROR_CHECK(localpo.get() == nullptr);
            }

            //
            // When a command is parsed, the result is a verb, a list of parameter values and a list
            // of options.
            //
            verb_t*                                               m_verb;
            std::vector<std::unique_ptr<parsed_parameter_t>>      m_parameters;
            std::vector<std::unique_ptr<parsed_option_t>>         m_options;
            std::map<sstring_t, m::not_null<parsed_parameter_t*>> m_parameter_map;
            std::map<sstring_t, m::not_null<parsed_option_t*>>    m_option_map;

            friend verb_t;
            friend parameter_t;
            friend option_t;
        };

        template <typename CharT>
        class option
        {
        public:
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using char_traits_t    = std::char_traits<char_t>;
            using string_view_t    = std::basic_string_view<char_t, char_traits_t>;
            using string_t         = std::basic_string<char_t, char_traits_t>;
            using parsed_command_t = parsed_command<char_t>;

            virtual ~option() = default;

            sstring_t
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

                auto fourspaces = "    "sv;

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

                it = std::ranges::copy(dashdash<CharT>, it).out;
                col += dashdash<CharT>.size();

                if (m_negatable)
                {
                    auto bracketnobracket = "[no-]"sv;
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

                auto TBD = "To be determined"sv;

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
            process_option(parsed_command_t& pc, int& index, int argc, char_t const* argv[])
            {
                if (index >= argc)
                    throw std::runtime_error("internal error - past end of command line arguments");

                auto const match_type = try_match(argv[index]);

                if (match_type == option_match_type::not_a_match)
                    return false;

                m_match_type = match_type;

                ++index;

                return do_process_option(pc, index, argc, argv);
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
                    if constexpr (std::is_same_v<char_t, char>)
                    {
                        m_negated_name = "no"sv + m_name;
                    }
                    else if constexpr (std::is_same_v<char_t, wchar_t>)
                    {
                        m_negated_name = L"no"sv + m_name;
                    }
                    else
                    {
                        // Should implement the other three char types
                        // and then not support non-char-types.
                        M_NOT_IMPLEMENTED("Only char and wchar_t implemented at this time");
                    }
                }
            }

            option_match_type
            try_match(string_view_t option)
            {
                if (auto x = starts_with_dashdash(option); x)
                {
                    auto rest = option.substr(2);

                    if (m_name.view().compare(rest) == 0)
                        return option_match_type::positive_match;
                    else if (m_negated_name.view().compare(rest) == 0)
                        return option_match_type::negated_match;
                }

                return option_match_type::not_a_match;
            }

            virtual bool
            do_process_option(parsed_command_t& pc, int& index, int argc, char_t const* argv[]) = 0;

            sstring_t         m_name;
            sstring_t         m_negated_name;
            bool              m_negatable;
            bool              m_defaulted;
            option_match_type m_match_type;
        };

        template <typename CharT>
        class boolean_option : public option<CharT>
        {
        public:
            using base_type_t      = option<CharT>;
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using char_traits_t    = std::char_traits<char_t>;
            using string_view_t    = std::basic_string_view<char_t, char_traits_t>;
            using parsed_command_t = parsed_command<char_t>;

            boolean_option(string_view_t name, bool negatable = true, bool default_value = false):
                option<char_t>(name, negatable),
                m_default_value(default_value),
                m_value(m_default_value)
            {}

        protected:
            bool
            do_process_option(parsed_command_t& pc, int&, int, char_t const*[]) override
            {
                std::ignore                 = pc;
                option<char_t>::m_defaulted = false;

                // The value itself tracks the "positivity" of the option
                if (option<char_t>::m_match_type == option_match_type::positive_match)
                    m_value = true;
                else
                    m_value = false;

                return true;
            }

            bool m_default_value;
            bool m_value;
        };

        template <typename CharT>
        class string_option : public option<CharT>
        {
        public:
            using base_type_t      = option<CharT>;
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using char_traits_t    = std::char_traits<char_t>;
            using string_view_t    = std::basic_string_view<char_t, char_traits_t>;
            using parsed_command_t = parsed_command<char_t>;

            string_option(string_view_t name,
                          string_view_t default_value = string_view_t{},
                          bool          negatable     = false):
                option<char_t>(name, negatable),
                m_default_value(default_value),
                m_value(m_default_value)
            {}

        protected:
            bool
            do_process_option(parsed_command_t& pc,
                              int&              index,
                              int               argc,
                              char_t const*     argv[]) override
            {
                std::ignore                 = pc;
                option<char_t>::m_defaulted = false;

                if (index >= argc)
                    throw std::runtime_error("too few command line arguments");

                m_value = argv[index++];
                return true;
            }

            sstring_t m_default_value;
            sstring_t m_value;
        };

        template <typename CharT>
        class path_option : public option<CharT>
        {
        public:
            using base_type_t      = option<CharT>;
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using char_traits_t    = std::char_traits<char_t>;
            using string_view_t    = std::basic_string_view<char_t, char_traits_t>;
            using parsed_command_t = parsed_command<char_t>;

            path_option(string_view_t name,
                        string_view_t default_value = string_view_t{},
                        bool          negatable     = false):
                option<char_t>(name, negatable),
                m_default_value(default_value),
                m_value(m_default_value)
            {}

        protected:
            bool
            do_process_option(parsed_command_t& pc,
                              int&              index,
                              int               argc,
                              char_t const*     argv[]) override
            {
                std::ignore                 = pc;
                option<char_t>::m_defaulted = false;

                if (index >= argc)
                    throw std::runtime_error("too few command line arguments");

                m_value = m::filesystem::make_path(argv[index]);

                index++;
                return true;
            }

            std::filesystem::path m_default_value;
            std::filesystem::path m_value;
        };

        template <typename CharT>
        class parameter
        {
        public:
            using char_t             = CharT;
            using sstring_t          = basic_sstring<char_t>;
            using string_view_t      = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using option_t           = option<char_t>;
            using parsed_command_t   = parsed_command<char_t>;
            using parsed_parameter_t = parsed_parameter<char_t>;
            using parsed_option_t    = parsed_option<char_t>;

            parameter()                 = delete;
            parameter(parameter const&) = delete;
            parameter(parameter&&)      = delete;
            void
            operator=(parameter const&) = delete;

            virtual ~parameter() = default;

            sstring_t
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

                it = std::ranges::copy(m_name.view(), it).out;

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
            process_parameter(parsed_command_t& pc, int& index, int argc, char_t const* argv[])
            {
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

                    do_process_parameter(pc, index, argc, argv);
                }
            }

        protected:
            parameter(string_view_t name, bool required):
                m_name(name), m_defaulted{}, m_required(required)
            {}

            static void
            add_parsed_parameter_to_parsed_command(parsed_command_t&                     pc,
                                                   std::unique_ptr<parsed_parameter_t>&& pp)
            {
                pc.add_parsed_parameter(std::move(pp));
            }

            static void
            add_parsed_option_to_parsed_command(parsed_command_t&                  pc,
                                                std::unique_ptr<parsed_option_t>&& po)
            {
                pc.add_parsed_option(std::move(po));
            }

            virtual void
            do_process_parameter(parsed_command_t& pc,
                                 int&              index,
                                 int               argc,
                                 char_t const*     argv[]) = 0;

            sstring_t m_name;
            bool      m_defaulted;
            bool      m_required;

            template <typename>
            class verb;
        };

        template <typename CharT>
        class path_parameter : public parameter<CharT>
        {
        public:
            using char_t             = CharT;
            using base_type_t        = parameter<char_t>;
            using sstring_t          = basic_sstring<char_t>;
            using string_view_t      = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using parsed_command_t   = parsed_command<char_t>;
            using parsed_parameter_t = parsed_parameter<char_t>;

            path_parameter(string_view_t          name,
                           bool                   required      = true,
                           string_view_t          default_value = string_view_t{}):
                parameter<CharT>(name, required),
                m_default_value(default_value),
                m_value(m::filesystem::make_path(m_default_value))
            {
            }

            path_parameter()                      = delete;
            path_parameter(path_parameter const&) = delete;
            path_parameter(path_parameter&&)      = delete;
            void
            operator=(path_parameter const&) = delete;

        protected:
            sstring_t             m_default_value;
            std::filesystem::path m_value;

            void
            do_process_parameter(parsed_command_t& pc,
                                 int&              index,
                                 int               argc,
                                 char_t const*     argv[]) override
            {
                if (index >= argc)
                {
                    if (parameter<char_t>::m_required)
                        throw std::runtime_error("missing required parameter");
                }
                else
                {
                    auto const path = m::filesystem::make_path(argv[index]);
                    m_value         = path;
                    base_type_t::add_parsed_parameter_to_parsed_command(
                        pc, std::make_unique<parsed_parameter_t>(this, path));
                    index++;
                    parameter<char_t>::m_defaulted = false;
                }
            }
        };

        template <typename CharT>
        class verb
        {
        public:
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using string_view_t    = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using parameter_t      = parameter<char_t>;
            using option_t         = option<char_t>;
            using parsed_command_t = parsed_command<char_t>;
            using verb_action_t    = std::function<void(parsed_command_t*)>;

            verb(string_view_t name, verb_action_t va): m_name(name), m_verb_action(va) {}

            verb()            = delete;
            verb(verb const&) = delete;
            verb(verb&&)      = delete;
            void
            operator=(verb const&) = delete;
            void
            operator=(verb&&) = delete;

            ~verb() = default;

            sstring_t
            name() const
            {
                return m_name;
            }

            template <typename Iter>
            Iter
            usage(Iter it)
            {
                it = std::ranges::copy(m_name.view(), it).out;

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

                    it = std::ranges::copy(p->name().view(), it).out;

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

                auto sometext = " [<Options>]\n\n"sv;

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

            path_parameter<char_t>&
            add_path_parameter(string_view_t name)
            {
                auto p1 = std::make_unique<path_parameter<char_t>>(name);

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

            path_option<char_t>&
            add_path_option(string_view_t name)
            {
                auto p1 = std::make_unique<path_option<char_t>>(name);

                auto const index = m_options.size();
                m_options.push_back(nullptr);

                m_options[index].reset(p1.release());
                return *m::try_cast<path_option<char_t>*>(m_options[index].get());
            }

            bool
            try_process_command_line(int& index, int argc, char_t const* argv[])
            {
                if (index >= argc)
                    throw std::runtime_error("argument count insufficient");

                if (!verb_matches(argv[index]))
                    return false;

                parsed_command_t pc;
                pc.verb(this);

                index++;

                //
                // This is kind of messed up; sort it out at some point.
                // Assumes that all parameters occur then all options
                // (switches).
                //

                for (auto&& p: m_parameters)
                    p->process_parameter(pc, index, argc, argv);

                while (index < argc)
                {
                    bool option_found = false;

                    for (auto&& e: m_options)
                    {
                        if (e->process_option(pc, index, argc, argv))
                        {
                            option_found = true;
                            break;
                        }
                    }

                    // Need a proper way to report errors to user??
                    if (!option_found)
                        throw std::runtime_error("invalid command line switch");
                }

                if (m_verb_action)
                    std::invoke(m_verb_action, &pc);

                return true;
            }

        protected:
            bool
            verb_matches(string_view_t arg)
            {
                return m_name.view().compare(arg) == 0;
            }

            sstring_t                                 m_name;
            verb_action_t                             m_verb_action;
            std::vector<std::unique_ptr<option_t>>    m_options;
            std::vector<std::unique_ptr<parameter_t>> m_parameters;
        };

        template <typename CharT>
        class command_verb_set
        {
        public:
            using char_t           = CharT;
            using sstring_t        = basic_sstring<char_t>;
            using string_view_t    = std::basic_string_view<char_t, std::char_traits<char_t>>;
            using string_t         = std::basic_string<char_t, std::char_traits<char_t>>;
            using option_t         = option<char_t>;
            using verb_t           = verb<char_t>;
            using verb_action_t    = verb_action<char_t>;
            using parsed_command_t = parsed_command<char_t>;

            command_verb_set() {}
            command_verb_set(command_verb_set const&) = delete;
            command_verb_set(command_verb_set&&)      = delete;

            void
            operator=(command_verb_set const&) = delete;

            verb_t&
            add_verb(string_view_t name, verb_action_t va = verb_action_t{})
            {
                auto p1 = std::make_unique<verb_t>(name, va);

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

            sstring_t
            usage(std::filesystem::path program)
            {
                auto filename  = program.filename();
                auto filename2 = m::filesystem::path_to_string(filename);

                using buffer_t = basic_string_buffer<char_t>;
                buffer_t buffer;
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
                        it = std::format_to(it, "[{}] ", v->name().c_str());
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
                // clang-format on

                return m::try_cast_helper<
                    basic_string_buffer_base<CharT,
                                             buffer_t::inline_value_count,
                                             typename buffer_t::derived_most_string_buffer_type>,
                    basic_sstring<char_t>,
                    void>::do_cast(buffer);

                // return m::to<basic_sstring<char_t>>(buffer);
            }

        protected:
            std::vector<std::unique_ptr<verb_t>> m_verbs;
        };

        template <typename CharT>
        void
        parse(int, CharT const*[])
        {
            //
            // We assume that this is the full argument set from main(), so
            // argv[0] is the program itself.
            //
        }
    } // namespace command_options
} // namespace m