// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <tuple>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/debugging/dbg_format.h>
#include <m/errors/errors.h>
#include <m/formatters/HRESULT.h>
#include <m/thread_description/thread_description.h>

#include "thread_description_impl.h"

namespace functors
{
    //
    // Not entirely sure where calling conventions go in using syntax for
    // function pointers. Omitting; only matter for x86. Bless your heart
    // if you are still using x86, feel free to submit a PR with a fix.
    //
    using get_thread_description_fn_ptr = HRESULT (*)(_In_ HANDLE, _Outptr_result_z_ PWSTR*);
    using set_thread_description_fn_ptr = HRESULT (*)(_In_ HANDLE, _In_ PCWSTR);

    HRESULT
    fake_get_thread_description(HANDLE, PWSTR* ppwsz)
    {
        *ppwsz      = reinterpret_cast<PWSTR>(::LocalAlloc(0, sizeof(WCHAR)));
        (*ppwsz)[0] = L'\0';
        return S_OK;
    }

    HRESULT
    fake_set_thread_description(HANDLE, PCWSTR) { return S_OK; }

    template <typename DerivedMostT, typename PointerTypeT>
    struct functor_base
    {
        functor_base(PCWSTR pwszDllName, PCSTR functionName)
        {
            HMODULE h = ::GetModuleHandleW(pwszDllName);
            // Shouldn't be able to fail!
            if (!h)
                m::throw_last_win32_error();

            m_pfn = reinterpret_cast<PointerTypeT>(::GetProcAddress(h, functionName));

            if (!m_pfn)
                m_pfn = &DerivedMostT::default_function;
        }

        template <typename ResultT, typename... Args>
        ResultT
        invoke_r(Args&&... args)
        {
            return (*m_pfn)(std::forward<Args>(args)...);
        }

        template <typename... Args>
        void
        invoke(Args&&... args)
        {
            (*m_pfn)(std::forward<Args>(args)...);
        }

    private:
        PointerTypeT m_pfn{};
    };

    // Use magic statics to perform one time initialization so that there is
    // minimal post-initialization cost for calling through.
    struct get_thread_description_functor :
        private functor_base<get_thread_description_functor, get_thread_description_fn_ptr>
    {
        get_thread_description_functor(): functor_base(L"KernelBase.dll", "GetThreadDescription") {}

        HRESULT
        operator()(HANDLE h, PWSTR * ppwsz) { return functor_base::invoke_r<HRESULT>(h, ppwsz); }

        static HRESULT
        default_function(HANDLE h, PWSTR* ppwsz)
        {
            return fake_get_thread_description(h, ppwsz);
        }
    };

    struct set_thread_description_functor :
        private functor_base<set_thread_description_functor, set_thread_description_fn_ptr>
    {
        set_thread_description_functor(): functor_base(L"KernelBase.dll", "SetThreadDescription") {}

        HRESULT
        operator()(HANDLE h, PCWSTR pcwsz) { return functor_base::invoke_r<HRESULT>(h, pcwsz); }

        static HRESULT
        default_function(HANDLE h, PCWSTR pcwsz)
        {
            return fake_set_thread_description(h, pcwsz);
        }
    };

    get_thread_description_functor get_thread_description;
    set_thread_description_functor set_thread_description;
} // namespace

void
m::thread_description_impl::set_thread_description(
    std::wstring_view                               description,
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    saved_state[0] = nullptr;

    HRESULT hr{};
    PWSTR   pwsz{};

    if (!SUCCEEDED(hr = functors::get_thread_description(::GetCurrentThread(), &pwsz)))
    {
        // There's no point in failing per se, we just will not set the thread description.
        m::dbg_format("Ignoring failed call to ::GetThreadDescription; hr = {}", fmtHRESULT(hr));
        return;
    }

    // The API needs a null terminated string, so ...
    auto const str   = std::wstring(description);
    auto const c_str = str.c_str();

    functors::set_thread_description(::GetCurrentThread(), c_str);

    saved_state[0] = (void*)pwsz;
}

void
m::thread_description_impl::restore_thread_description(
    m::thread_description_impl::saved_thread_state& saved_state) noexcept
{
    if (saved_state[0] != nullptr)
    {
        PWSTR pwsz = (PWSTR)saved_state[0];
        functors::set_thread_description(::GetCurrentThread(), pwsz);
        ::LocalFree(pwsz);
        saved_state[0] = nullptr;
    }
}
