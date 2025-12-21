// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <format>
#include <optional>
#include <string_view>

#include <m/error_handling/macros.h>
#include <m/tracing/format_view.h>
#include <m/tracing/tracing.h>

namespace m
{
    namespace tracing
    {
        struct frame
        {
            template <typename TThis>
            frame(char const* function_name, TThis* thisptr) noexcept:
                m_function_name(function_name), m_thisptr(reinterpret_cast<uintptr_t>(thisptr))
            {
                m::wtrace_verbose(L"Entering {:x}->{}", reinterpret_cast<uintptr_t>(thisptr), m_function_name);
            }

            frame(char const* function_name) noexcept:
                m_function_name(function_name), m_thisptr(std::nullopt)
            {
                m::wtrace_verbose(L"Entering {}", m_function_name);
            }

            template <typename... Types>
            void
            verbose(const std::wformat_string<Types...> fmt, Types&&... args)
            {
                wtrace_verbose(fmt, std::forward<Types>(args)...);
            }

            ~frame()
            {
                if (m_succeeded)
                {
                    if (m_thisptr.has_value())
                        m::wtrace_verbose(L"Exiting {:x}->{}", m_thisptr.value(), m_function_name);
                    else
                        m::wtrace_verbose(L"Exiting {}", m_function_name);
                }
                else
                {
                    if (m_thisptr.has_value())
                        m::wtrace_error(
                            L"Failed! Exiting {:x}->{}", m_thisptr.value(), m_function_name);
                    else
                        m::wtrace_error(L"Failed! Exiting {}", m_function_name);
                }
            }

            template <typename T>
            decltype(auto)
            succeeded(T&& v)
            {
                m_succeeded = true;
                return std::forward<T>(v);
            }

            template <typename T>
            decltype(auto)
            succeededv(T const& v)
            {
                m_succeeded = true;
                return static_cast<T>(v);
            }

            void
            succeeded()
            {
                m_succeeded = true;
            }

        private:
            m::tracing::format_view<char> m_function_name;
            std::optional<std::uintptr_t> m_thisptr;
            bool                          m_succeeded{false};
        };
    } // namespace tracing
} // namespace m
