// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <deque>
#include <format>
#include <iterator>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <m/utility/exception.h>
#include <m/utility/zstring.h>

//
// This is specifically about managing and sometimes reporting errors.
//
// It is not about reporting warnings, informational, verbose or any such
// information. There is already a facility in `m` for doing so, check out
// `tracing.h`.
//
// As such, there are only two varieties of "errors" described here. "Fatal"
// errors which terminate the process immediately and "non-fatal" which are
// still errors and generally fit into the patterns but which may turn into
// C++ exceptions which clients may catch and continue operation.
//
// There is no expectation to continue execution past when a "fatal" error
// is detected, but when a "non-fatal" error is detected, we expect that
// the condition may be handled and execution resumed. Some set of objects
// in the address space may be poisoned and not work correctly but they
// must not lead to general "undefined behavior" in the C++ sense. If their
// operations are not noexcept, they may throw exceptions indicating that
// they cannot work at this time, they can return error codes, etc. The
// process overall may have no practical recourse other than to terminate
// and restart, but undefined behavior (access violations, generally
// behaviors which turn into thinig which are easily exploitable) must not
// be permitted.
//
// Note that even the "inoperable objects" are generally bad states. They
// can result in a denial of service attack, even if they do not fit the
// "undefined behavior" kind of exploit described.
//
// this is only the tip of the iceberg and really the overall error handling
// philosophy can only be determined by the nature of your application.
// Modern services usually are designed to fail fast and restart quickly,
// but this would not be appropriate for life critical single instance
// applications.
//

namespace m::error_macros
{
    using error_handler_fn_t = void (*)(bool                        fatal,
                                        std::source_location const* optional_src_location,
                                        std::string_view            text);

    class global_error_handling_callback_registration_token
    {
    public:
        virtual ~global_error_handling_callback_registration_token() = default;
    };

    class global_error_list
    {
        struct entry : public global_error_handling_callback_registration_token
        {
            constexpr entry(error_handler_fn_t error_handler_fn) noexcept:
                m_error_handler_fn(error_handler_fn)
            {}
            ~entry() = default;

            error_handler_fn_t m_error_handler_fn;
        };

    public:
        struct token_deleter
        {
        public:
            token_deleter() = delete;
            constexpr token_deleter(global_error_list* gfel) noexcept: m_gfel(gfel) {}
            constexpr token_deleter(token_deleter const& other) noexcept: m_gfel(other.m_gfel) {}
            constexpr token_deleter(token_deleter&& other) noexcept: m_gfel(other.m_gfel) {}
            ~token_deleter() = default;

            inline token_deleter&
            operator=(token_deleter const& other) noexcept
            {
                m_gfel = other.m_gfel;
                return *this;
            }

            inline token_deleter&
            operator=(token_deleter&& other) noexcept
            {
                using std::swap;
                swap(m_gfel, other.m_gfel); // probably could just be assignment??
                return *this;
            }

            inline void
            operator()(global_error_handling_callback_registration_token* ptr) noexcept
            {
                m_gfel->unregister_handler(ptr);

                std::unique_ptr<global_error_handling_callback_registration_token> up;
                up.reset(ptr);
                up.reset(); // explicit call to reset here so there's a place to set a breakpoint if
                            // needed
            }

        private:
            global_error_list* m_gfel;
        };

        inline global_error_list() = default;

        inline std::unique_ptr<global_error_handling_callback_registration_token, token_deleter>
        register_handler(error_handler_fn_t error_handler_fn)
        {
            auto l = std::unique_lock(m_mutex);

            auto e = std::make_unique<entry>(error_handler_fn);
            m_deque.push_back(e.get());

            std::unique_ptr<global_error_handling_callback_registration_token, token_deleter>
                fancy_up(e.release(), token_deleter(this));
            return fancy_up;
        }

        inline void
        unregister_handler(global_error_handling_callback_registration_token* token)
        {
            auto e = dynamic_cast<entry*>(token);

            auto l = std::unique_lock(m_mutex);

            auto it  = m_deque.begin();
            auto end = m_deque.end();

            while (it != end)
            {
                if (*it == e)
                {
                    m_deque.erase(it);
                    break;
                }
                ++it;
            }

            std::unique_ptr<entry> up(e);
            up.reset(); // explicit reset to give debuggers (human) a place to set a breakpoint
        }

        inline void
        von_error(std::source_location const* optional_src_location,
                  std::string_view            fmt,
                  std::format_args            args)
        {
            std::string buffer;
            auto        it = std::back_inserter(buffer);
            std::vformat_to(it, fmt, args);
            dispatch_failure(
                false, optional_src_location, std::string_view(buffer.begin(), buffer.end()));
        }

        template <typename... Args>
        inline void
        on_error(std::source_location const* optional_source_location,
                 std::format_string<Args...> fmt,
                 Args&&... args)
        {
            von_error(optional_source_location, fmt.get(), std::make_format_args(args...));
        }

        [[noreturn]]
        inline void
        von_fatal(std::source_location const* optional_src_location,
                  std::string_view            fmt,
                  std::format_args            args)
        {
            std::string buffer;
            auto        it = std::back_inserter(buffer);
            std::vformat_to(it, fmt, args);
            dispatch_failure(
                true, optional_src_location, std::string_view(buffer.begin(), buffer.end()));
            std::abort();
        }

        template <typename... Args>
        inline void
        on_fatal(std::source_location const* optional_source_location,
                 std::format_string<Args...> fmt,
                 Args&&... args)
        {
            von_fatal(optional_source_location, fmt.get(), std::make_format_args(args...));
        }

    private:
        inline void
        dispatch_failure(bool                        fatal,
                         std::source_location const* optional_src_location,
                         std::string_view            text)
        {
            auto l = std::unique_lock(m_mutex);

            auto it  = m_deque.begin();
            auto end = m_deque.end();

            while (it != end)
            {
                (*(*it)->m_error_handler_fn)(fatal, optional_src_location, text);
                it++;
            }
        }

        std::mutex         m_mutex;
        std::deque<entry*> m_deque;
    };

    inline static global_error_list gs_global_error_list;

    inline std::unique_ptr<global_error_handling_callback_registration_token,
                           global_error_list::token_deleter>
    register_global_error_handler(error_handler_fn_t error_handler_fn)
    {
        return gs_global_error_list.register_handler(error_handler_fn);
    }

    inline void
    von_error(std::source_location const* optional_src_location,
              std::string_view            fmt,
              std::format_args            args)
    {
        gs_global_error_list.von_error(optional_src_location, fmt, args);
    }

    template <typename... Args>
    inline void
    on_error(std::source_location const* optional_source_location,
             std::format_string<Args...> fmt,
             Args&&... args)
    {
        m::error_macros::von_error(
            optional_source_location, fmt.get(), std::make_format_args(args...));
    }

    template <typename... Args>
    inline void
    on_error(std::source_location const& source_location,
             std::format_string<Args...> fmt,
             Args&&... args)
    {
        m::error_macros::von_error(&source_location, fmt.get(), std::make_format_args(args...));
    }

    [[noreturn]]
    inline void
    von_fatal(std::source_location const* optional_src_location,
              std::string_view            fmt,
              std::format_args            args)
    {
        gs_global_error_list.von_fatal(optional_src_location, fmt, args);
    }

    template <typename... Args>
    [[noreturn]]
    inline void
    on_fatal(std::source_location const* optional_source_location,
             std::format_string<Args...> fmt,
             Args&&... args)
    {
        m::error_macros::von_fatal(
            optional_source_location, fmt.get(), std::make_format_args(args...));
    }

    template <typename... Args>
    [[noreturn]]
    inline void
    on_fatal(std::source_location const& source_location,
             std::format_string<Args...> fmt,
             Args&&... args)
    {
        m::error_macros::von_fatal(&source_location, fmt.get(), std::make_format_args(args...));
    }
} // namespace m::error_macros

#define M_FAIL_FAST_NO_TEXT()                                                                      \
    do                                                                                             \
    {                                                                                              \
        /* m::tracing::monitor->close(m::tracing::close_flush_option::expedite); */                \
        m::error_macros::on_fatal(std::source_location::current(), "");                            \
    } while (false)

//
// M_INTERNAL_ERROR_CHECK is a lot like assert() except that it's always on
// even in retail code. It is meant to afford swift justice to offenders.
//
// It is much like the proposed contract_assert() in C++26 although the
// effects of contract_assert() violations are not yet specified. Chances
// are that it will be specified to call std::abort() which is effectively
// if not literally what M_INTERNAL_ERROR_CHECK() does when the expression
// evaluates falsy.
//

#define M_INTERNAL_ERROR_CHECK(e)                                                                  \
    do                                                                                             \
    {                                                                                              \
        bool const m_m_internal_v = !!(e);                                                         \
        if (!m_m_internal_v)                                                                       \
        {                                                                                          \
            m::error_macros::on_fatal(                                                             \
                std::source_location::current(), "Internal error check failed: \"{}\"", #e);       \
        }                                                                                          \
    } while (false)

//
// M_DEBUG_INTERNAL_ERROR_CHECK() is M_INTERNAL_ERROR_CHECK but only
// when NDEBUG is not defined, much like assert(). "So what's the difference
// between it and assert?" assert() may do other things other than just
// terminate the program.
//
// Probably most asserts coming from standard library implementations
// are fine but in practice is seems like everyone defines their own
// assert() and it can do things like pop up windowed user interface
// dialogs which is not an acceptable behavior if the application is
// a TCP/IP service.
//

#ifdef NDEBUG

#define M_DEBUG_INTERNAL_ERROR_CHECK(e)

#else

#define M_DEBUG_INTERNAL_ERROR_CHECK(e) M_INTERNAL_ERROR_CHECK((e))

#endif

//
// Pre-conditions and post-conditions are specialized internal error checks.
//
// They are reported as internal error checks but their semantic intent
// is that preconditions are validated at the entry to blocks (usually
// function level) and postconditions are validated on exit.
//
// Postcondition checks are relatively difficult to phrase as macros since
// functions may exit early. Some work may need to be done here to perhaps
// capture the condition validation into a lambda that is validated via a
// RAII destructor invocation. This may be tricky because to get the correct
// semantics, the object must be a l-value, e.g. a named variable, the value
// of which is preserved until the scope terminates. But there is no way to
// have multiple macro invocations define multiple variables.
//

#define M_VERIFY_PRECONDITION(c)                                                                   \
    do                                                                                             \
    {                                                                                              \
        bool const m_m_internal_v = !!(c);                                                         \
        if (!m_m_internal_v)                                                                       \
        {                                                                                          \
            m::error_macros::on_fatal(                                                             \
                std::source_location::current(), "Precondition failed: \"{}\"", #c);               \
        }                                                                                          \
    } while (false)

//
// Note that M_VERIFY_POSTCONDITION() does not work like an "assert()" macro.
//
// It is a "delayed assert" macro which performs its verification at scope
// exit, whether due to normal scope exit or exception rundown. So be sure
// to put true invariant postconditions here, and put these at the TOPS
// of your scopes you want to protect, not at the bottoms where you might
// think you should put your postconditions, since they actually "run" code
// to define the RAII object with the destructor to execute the postcondition
// check.
//

#define M_VERIFY_POSTCONDITION(c)                                                                  \
    auto const m_m_postcondition_lambda_##__LINE__ = [&] {                                         \
        bool const m_m_internal_v = !!(c);                                                         \
        if (!m_m_internal_v)                                                                       \
        {                                                                                          \
            m::error_macros::on_fatal(                                                             \
                std::source_location::current(), "Postcondition failed: \"{}\"", #c);              \
        }                                                                                          \
    };                                                                                             \
    template <typename MVerifyPostconditionLambdaT>                                                \
    struct m_m_postcondition_runner_##__LINE__                                                     \
    {                                                                                              \
        constexpr m_m_postcondition_runner_##__LINE__(MVerifyPostconditionLambdaT lambda) noexcept \
            :                                                                                      \
            m_lambda(lambda)                                                                       \
        {}                                                                                         \
        ~m_m_postcondition_runner_##__LINE__() { m_lambda(); }                                     \
        MVerifyPostconditionLambdaT m_lambda;                                                      \
    } m_m_postcondition_runner_##__LINE__##_instance(m_m_postcondition_lambda_##__LINE__);         \
    do                                                                                             \
    {                                                                                              \
    } while (false)

#define M_UNREACHABLE_CODE()                                                                       \
    do                                                                                             \
    {                                                                                              \
        M_INTERNAL_ERROR_CHECK(!"this code should not be reachable");                              \
    } while (false)

#define M_NOT_IMPLEMENTED(text)                                                                    \
    do                                                                                             \
    {                                                                                              \
        m::error_macros::on_error(std::source_location::current(), "Not implemented: '{}'", text); \
        throw m::not_implemented(text);                                                            \
    } while (false)

#define M_CHECK_OR_NOT_IMPLEMENTED(expr, text)                                                     \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = !!(expr);                                                  \
        if (!m_m_internal_value)                                                                   \
        {                                                                                          \
            m::error_macros::on_error(std::source_location::current(),                             \
                                      "Test failed: {}; not implemented. {}",                      \
                                      #expr,                                                       \
                                      text);                                                       \
            throw m::not_implemented(text);                                                        \
        }                                                                                          \
    } while (false)

#define M_VALIDATE_PARAMETER(pname, expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = !!(expr);                                                  \
        if (!m_m_internal_value)                                                                   \
        {                                                                                          \
            m::error_macros::on_error(std::source_location::current(),                             \
                                      "Parameter '{}' failed validation expression: '{}'",         \
                                      #pname,                                                      \
                                      #expr);                                                      \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

#define M_VALIDATE_PARAMETER_NOT_NULLPTR(pname)                                                    \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = (pname);                                                   \
        if (pname == nullptr)                                                                      \
        {                                                                                          \
            m::error_macros::on_error(std::source_location::current(),                             \
                                      "Parameter '{}' failed validation. Must not be nullptr.",    \
                                      #pname);                                                     \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

namespace m::macros_impl
{
    /// <summary>
    /// m::macros_impl::integral_type_for_t<T> yields either the underlying type for the
    /// enumeration type T, or T if it is an integral type.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    template <typename T>
        requires(std::integral<T> || std::is_enum_v<T>)
    using integral_type_for_t = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
} // namespace m::macros_impl

#define M_VALIDATE_FLAGS_PARAMETER(pname, valid_flags)                                             \
    do                                                                                             \
    {                                                                                              \
        auto m_m_internal_value       = (pname);                                                   \
        using m_m_internal_value_type = decltype(m_m_internal_value);                              \
        using m_m_integral_type =                                                                  \
            m::macros_impl::integral_type_for_t<decltype(m_m_internal_value)>;                     \
        static_assert(std::integral<m_m_internal_value_type> ||                                    \
                      std::is_enum_v<m_m_internal_value_type>);                                    \
        m_m_internal_value_type m_m_internal_valid_flags = (valid_flags);                          \
        m_m_internal_value_type m_m_internal_excess_flags =                                        \
            m_m_internal_value & ~m_m_internal_valid_flags;                                        \
        if (static_cast<bool>(m_m_internal_excess_flags))                                          \
        {                                                                                          \
            m::error_macros::on_error(std::source_location::current(),                             \
                                      "Flags parameter '{}' has excess flags set: {:#x}",          \
                                      #pname,                                                      \
                                      static_cast<m_m_integral_type>(m_m_internal_excess_flags));  \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

#define M_API_PARAMETER_MUST_BE_ZERO(api, p)                                                       \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_parameter_value = (p);                                             \
        auto const m_m_internal_parameter_reference_value =                                        \
            decltype(m_m_internal_parameter_value){};                                              \
        if (m_m_internal_parameter_value != m_m_internal_parameter_reference_value)                \
        {                                                                                          \
            m::error_macros::on_error(std::source_location::current(),                             \
                                      "Parameter '{}' failed MBZ validation in api '{}'",          \
                                      #p,                                                          \
                                      #api);                                                       \
            throw m::invalid_parameter(api "." #p);                                                \
        }                                                                                          \
    } while (false)
