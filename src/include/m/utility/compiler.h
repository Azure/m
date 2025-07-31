// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>
#include <version>

#ifdef __clang__

#define M_NOINLINE __attribute__((noinline))

#elifdef _MSC_VER

#define M_NOINLINE __declspec(noinline)

#else

#error unsupported compiler

#endif


