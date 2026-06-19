// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <system_error>

#include <m/errors/errors.h>
#include <m/utility/exception.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::mwin32_impl
{
    //
    // This header translates **exceptions and `std::error_code`s** into Win32
    // error codes. Those are cross-cutting facts carried by the thrown object
    // (its category, or the system_error's embedded code), independent of which
    // operation raised them, so a single translator is correct and shareable.
    //
    // It is NOT, and must never become, a PIL `disposition` mapper. Each PIL
    // verb owns its own flags / result-code / result-flags enums (declared on
    // the specific virtual member function), and those vocabularies are not
    // interchangeable across verbs. A disposition is always interpreted at the
    // call site of the specific verb that produced it; see DESIGN-NOTES D12.
    //
    //
    // If `se` carries a Win32 status that was mapped into an HRESULT by
    // HRESULT_FROM_WIN32() (severity bit set, FACILITY_WIN32, not an NTSTATUS),
    // recover the original Win32 error code. Returns nullopt for any other
    // system_error so the caller can decide how to handle it.
    //
    inline std::optional<DWORD>
    decode_win32_error(std::system_error const& se)
    {
        auto const& code = se.code();
        if (code.category() == m::hresult_category())
        {
            // The fact that it's in the HRESULT category means that we can
            // perform this cast with (without?) impunity.
            auto value = static_cast<HRESULT>(code.value());

            // If it's not an NTSTATUS mapped into an HRESULT, and the severity
            // bit is set, and the facility is FACILITY_WIN32, this was "created"
            // by HRESULT_FROM_WIN32() so we'll unmap it.
            if (((value & FACILITY_NT_BIT) == 0) && (HRESULT_SEVERITY(value)) &&
                (HRESULT_FACILITY(value) == FACILITY_WIN32))
            {
                return HRESULT_CODE(value);
            }
        }

        return std::nullopt;
    }

    //
    // Map the in-flight C++ exception raised while servicing a shim entry point
    // to its Win32 error code. MUST be called from within a catch block: it
    // rethrows the active exception so the dynamic type can be matched. The
    // recognized categories are:
    //
    //   * the m:: fault categories raised by the fault-injection layer and by
    //     the platform-neutral providers
    //   * m::invalid_parameter raised by M_VALIDATE_PARAMETER in the entry
    //     points
    //   * std::system_error carrying a Win32/HRESULT code (via
    //     decode_win32_error)
    //
    // Returns nullopt for an exception the shim does not recognize. The caller
    // decides what to do with an unrecognized exception: the registry entry
    // points rethrow it (preserving the prior propagation behavior); the
    // filesystem entry points, which must not let an exception cross the C ABI,
    // substitute a generic failure status.
    //
    inline std::optional<DWORD>
    map_known_pil_exception()
    {
        try
        {
            throw;
        }
        catch (m::not_found const&)
        {
            return ERROR_FILE_NOT_FOUND;
        }
        catch (m::access_denied const&)
        {
            return ERROR_ACCESS_DENIED;
        }
        catch (m::sharing_violation const&)
        {
            return ERROR_SHARING_VIOLATION;
        }
        catch (m::already_exists const&)
        {
            return ERROR_ALREADY_EXISTS;
        }
        catch (m::out_of_resources const&)
        {
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        catch (m::not_supported const&)
        {
            return ERROR_NOT_SUPPORTED;
        }
        catch (m::invalid_parameter const&)
        {
            return ERROR_INVALID_PARAMETER;
        }
        catch (std::system_error const& se)
        {
            return decode_win32_error(se);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    //
    // Map the in-flight C++ exception to an HRESULT for entry points that return
    // HRESULT (like the HWC shims). MUST be called from within a catch block.
    // Similar to map_known_pil_exception but returns HRESULT directly instead of
    // a Win32 DWORD. Unrecognized exceptions map to E_FAIL so that nothing
    // escapes across the C ABI.
    //
    inline HRESULT
    map_pil_exception_to_hresult()
    {
        try
        {
            throw;
        }
        catch (m::not_found const&)
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }
        catch (m::access_denied const&)
        {
            return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        }
        catch (m::sharing_violation const&)
        {
            return HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
        }
        catch (m::already_exists const&)
        {
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
        catch (m::out_of_resources const&)
        {
            return E_OUTOFMEMORY;
        }
        catch (m::not_supported const&)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        catch (m::not_implemented const&)
        {
            return E_NOTIMPL;
        }
        catch (m::invalid_parameter const&)
        {
            return E_INVALIDARG;
        }
        catch (std::bad_alloc const&)
        {
            return E_OUTOFMEMORY;
        }
        catch (std::system_error const& se)
        {
            auto const win32 = decode_win32_error(se);
            if (win32.has_value())
                return HRESULT_FROM_WIN32(win32.value());
            // If it's already an HRESULT category, return it directly.
            if (se.code().category() == m::hresult_category())
                return static_cast<HRESULT>(se.code().value());
            // Otherwise, generic failure.
            return E_FAIL;
        }
        catch (...)
        {
            return E_FAIL;
        }
    }

} // namespace m::mwin32_impl
