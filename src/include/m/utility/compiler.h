// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>
#include <version>

#undef M_HAS_CXX23

#ifdef __clang__

#define M_NOINLINE __attribute__((noinline))

#if __cplusplus >= 202302L
#define M_HAS_CXX23
#endif

#elifdef _MSC_VER

#define M_NOINLINE __declspec(noinline)

#if _MSVC_LANG >= 202302L
#define M_HAS_CXX23
#endif

#else

#error unsupported compiler

#endif

#ifndef M_HAS_CXX23

#error The M library requires C++23

#endif


