// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <tuple>

#include <m/thread_description/thread_description.h>

void
m::thread_description_impl::set_thread_description(
    std::wstring_view                               description,
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    std::ignore = description;
    std::ignore = saved_state;
}

void
m::thread_description_impl::restore_thread_description(
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    std::ignore = saved_state;
}

