// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>
#include <version>

#undef M_HAS_CXX23
#undef M_HAS_CXX20

#undef M_HAS_MSVC
#undef M_HAS_CLANG
#undef M_HAS_GCC

#if defined(_MSC_VER) && !defined(__clang__)

#define M_HAS_MSVC 1

#elif defined(__clang__)

#define M_HAS_CLANG 1

#elif defined(__GNUC__)

#define M_HAS_GCC 1

#else

#error Unsupported compiler

#endif

#if M_HAS_MSVC

#define M_STRINGIZE(x)  #x
#define M_XSTRINGIZE(x) M_STRINGIZE(x)

#ifdef M_DEBUG_COMPILER_VERSION
#pragma message("MSVC: _MSC_VER: " M_XSTRINGIZE(_MSC_VER))
#pragma message("MSVC: _MSVC_LANG: " M_XSTRINGIZE(_MSVC_LANG))
#endif

#define M_NOINLINE __declspec(noinline)

#if _MSVC_LANG >= 202002L
#define M_HAS_CXX20 1
#endif

#if _MSVC_LANG >= 202302L
#define M_HAS_CXX23 1
#endif

#elif M_HAS_CLANG

#define M_NOINLINE __attribute__((noinline))

#if __cplusplus >= 202302L
#define M_HAS_CXX23 1
#endif

#if __cplusplus >= 202002L
#define M_HAS_CXX20 1
#endif

#elif M_HAS_GCC

#define M_NOINLINE __attribute__((noinline))

#if __cplusplus >= 202302L
#define M_HAS_CXX23 1
#endif

#if __cplusplus >= 202002L
#define M_HAS_CXX20 1
#endif

#else

#error unsupported compiler

#endif

//
// What language standard do we require?
//
// This is hard to easily answer.
//
// While trying to take advantage of even preliminary C++26 features (it is
// August 2025 as of this writing) would be folly, the m library is taking
// advantage of C++23 features wherever they are advantageous.
//
// There are relatively few large, conspicuous C++23 features, so it's difficult
// to characterize large portions of the m library as being dependent on
// C++23 vs. C++20.
//
// On the other hand, C++23 support is still sketchy, broadly. clang has good
// language support from the compiler but the libraries are not the best.
// MSVC has good library support for standard library features but the
// compiler/language support is lacking, so the compiler does not even
// identify itself as "C++23", you can only compile with std=c++latest
// which gets you a version higher than c++20 but ...
//
// In the end, the hard block is here on C++20 because there is rampant
// C++20 usage across the board, and there's no way that there will be
// conditional use of C++20 features, or explicit errors in headers which
// need to declare their C++20 support.
//
// Also, MSVC has not caught up the managed code (`/clr`) feature to be
// able to build managed C++ quite yet. The most recent compilers can build
// C++20 with managed code enabled so update your compiler if this is not
// working for you.
//
// If there is some sub-feature which could be finessed to work optionally
// with/without C++23, submit a PR! We're not against the conditionality,
// it's just not how we're going to write the code initially. Be sure to
// include testing that tests building both ways.
//

#ifndef M_HAS_CXX20

#error The M library requires C++20

#endif
