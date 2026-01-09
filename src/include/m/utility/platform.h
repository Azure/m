// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>
#include <version>

#include <m/utility/compiler.h>

#undef M_HAS_WIN32

#if _WIN32 || WIN32

#define M_HAS_WIN32 1

#else

#define M_HAS_WIN32 0

#endif

#undef M_WCHAR_T_IS_UTF16

#if M_HAS_WIN32

static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(char16_t) == 2);

#define M_WCHAR_T_IS_UTF16 1

#else

static_assert(sizeof(wchar_t) != 2);
static_assert(sizeof(char16_t) == 2);

#define M_WCHAR_T_IS_UTF16 0

#endif
