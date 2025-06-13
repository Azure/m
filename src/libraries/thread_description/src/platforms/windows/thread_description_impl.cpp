// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <tuple>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/debugging/dbg_format.h>
#include <m/formatters/HRESULT.h>
#include <m/thread_description/thread_description.h>

#include "thread_description_impl.h"

void
m::thread_description_impl::set_thread_description(
    std::wstring_view                               description,
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    saved_state[0] = nullptr;

    HRESULT hr{};
    PWSTR   pwsz{};

    if (!SUCCEEDED(hr = ::GetThreadDescription(::GetCurrentThread(), &pwsz)))
    {
        // There's no point in failing per se, we just will not set the thread description.
        m::dbg_format("Ignoring failed call to ::GetThreadDescription; hr = {}", fmtHRESULT(hr));
        return;
    }

    // The API needs a null terminated string, so ...
    auto const str   = std::wstring(description);
    auto const c_str = str.c_str();

    ::SetThreadDescription(::GetCurrentThread(), c_str);

    saved_state[0] = (void*)pwsz;
}

void
m::thread_description_impl::restore_thread_description(
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    if (saved_state[0] != nullptr)
    {
        PWSTR pwsz = (PWSTR)saved_state[0];
        SetThreadDescription(::GetCurrentThread(), pwsz);
        ::LocalFree(pwsz);
        saved_state[0] = nullptr;
    }
}
