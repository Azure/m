// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/strings/literal_string_view.h>
#include <m/tracing/event_kind.h>
#include <m/tracing/imessage.h>
#include <m/utility/pointers.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        class message;
        class message_source;

        class envelope
        {
        public:
            envelope() = delete;
            envelope(m::not_null<tracing::message_source*> source, imessage* msg = nullptr);

            envelope(envelope const& other) = delete;
            envelope(envelope&& other) noexcept;

            void
            operator=(envelope const& other) = delete;

            void
            operator=(envelope&& other) noexcept;

            void
            swap(envelope& other) noexcept;

            tracing::imessage*
            message() const;

            tracing::imessage*
            message(tracing::imessage* msg);

            m::not_null<tracing::message_source*>
            message_source() const;

            ~envelope();

        private:
            m::not_null<tracing::message_source*> m_message_source;
            tracing::imessage*                    m_imessage{};
        };
    } // namespace tracing
} // namespace m
