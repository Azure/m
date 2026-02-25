# GCC Build Checklist

Build configuration: `x64-linux-gnuc-debug` (GCC 13.3.0, x64, Linux, C++20)

---

## Summary

**11 compilation units across 5 library sub-projects fail to build.**
All failures trace back to **two root causes** in shared headers:

| Root Cause | File | Fixable |
|---|---|---|
| 1 | `src/include/m/utility/compiler.h` — no GCC branch | ✅ Yes |
| 2 | `src/libraries/strings/include/m/strings/literal_string_view.h` — GCC rejects `friend` declarations for UDL operators with qualified namespace names | ✅ Yes |

Fixing these two headers would resolve all five library failures.

---

## Root Cause 1 — `src/include/m/utility/compiler.h`

**Problem:** The file explicitly `#error`s for any compiler that is neither MSVC nor
Clang:

```cpp
#if defined(_MSC_VER) && !defined(__clang__)
    #define M_HAS_MSVC 1
#elif defined(__clang__)
    #define M_HAS_CLANG 1
#else
    #error Unsupported compiler   // ← GCC hits this
#endif
```

A second `#else #error unsupported compiler` repeats this for the `M_NOINLINE`
and C++ standard detection blocks, and then `M_HAS_CXX20` is never defined so
a third `#error The M library requires C++20` fires.

**Fix:** Add a `#elif defined(__GNUC__)` branch in all three gated blocks:
- Define `M_HAS_GCC 1`
- Define `M_NOINLINE __attribute__((noinline))` (identical to the Clang path)
- Detect C++20/C++23 via `__cplusplus >= 202002L` / `>= 202302L` (same as Clang)

**Assessment: ✅ Straightforward — identical to the existing Clang branch.**

---

## Root Cause 2 — `src/libraries/strings/include/m/strings/literal_string_view.h`

**Problem:** GCC rejects `friend` declarations for user-defined literal (UDL)
operators when the operator is named with a fully-qualified nested-namespace name
inside a class body:

```cpp
// GCC error: "has invalid argument list"
friend constexpr basic_literal_string_view<char>
    string_view_literals::operator""_sl(const char*, std::size_t);
```

Because GCC rejects the friend declaration, the UDL operators cannot access the
`protected` constructor, producing a second error:
`'basic_literal_string_view(const CharT*, std::size_t)' is protected within this context`.

This is a known GCC strictness difference from Clang/MSVC. The standard is
ambiguous here and GCC takes the stricter reading.

**Fix options (any one suffices):**
1. Replace the `protected` constructor + `friend` pattern with a private tag-type
   trick: add a private `struct key {}`, make the constructor `public` but require
   a `key` argument, and have the UDL operators pass `key{}`. No friend declarations
   needed.
2. Move the UDL operator definitions into the same namespace as the class (`m`) and
   declare them `inline friend` inside the class body rather than using qualified names.

**Assessment: ✅ Fixable with a small structural refactor. No external API change is
required if option 1 is used (the constructor remains effectively uncallable from
outside the class without the tag type).**

---

## Failing Library Sub-Projects

### `src/libraries/byte_streams`

| | |
|---|---|
| **Failing units** | `src/byte_streams.cpp`, `src/memory_stream.cpp` |
| **Error** | `compiler.h:27: #error Unsupported compiler` |
| **Root cause** | Root Cause 1 only |
| **Fixable?** | ✅ Yes — fixed automatically once `compiler.h` gains a GCC branch |

---

### `src/libraries/debugging`

| | |
|---|---|
| **Failing units** | `src/dbg_format.cpp` |
| **Error** | `compiler.h:27: #error Unsupported compiler` |
| **Root cause** | Root Cause 1 only |
| **Fixable?** | ✅ Yes — fixed automatically once `compiler.h` gains a GCC branch |

---

### `src/libraries/io`

| | |
|---|---|
| **Failing units** | `src/io.cpp` |
| **Error** | `compiler.h:27: #error Unsupported compiler` |
| **Root cause** | Root Cause 1 only |
| **Fixable?** | ✅ Yes — fixed automatically once `compiler.h` gains a GCC branch |

---

### `src/libraries/tracing`

| | |
|---|---|
| **Failing units** | `src/message_source.cpp`, `src/sink.cpp` |
| **Errors** | (1) `compiler.h:27: #error Unsupported compiler` via `tracing.h` → `frame.h` → `compiler.h`; (2) `literal_string_view.h:50: UDL operator has invalid argument list` via `sink.h` → `envelope.h` → `literal_string_view.h` |
| **Root cause** | Root Causes 1 **and** 2 |
| **Fixable?** | ✅ Yes — requires both root-cause fixes |

---

### `src/libraries/utf`

| | |
|---|---|
| **Failing units** | `src/decode_utf16.cpp`, `src/decode_utf32.cpp`, `src/decode_utf8.cpp`, `src/encoding_size.cpp`, `src/utf.cpp` |
| **Error** | `compiler.h:27: #error Unsupported compiler` |
| **Root cause** | Root Cause 1 only |
| **Fixable?** | ✅ Yes — fixed automatically once `compiler.h` gains a GCC branch |

---

## Libraries and Tests That Build Successfully

The following libraries built cleanly under GCC and are not affected:

- `src/libraries/arefc_ptr`
- `src/libraries/atomic`
- `src/libraries/bitset`
- `src/libraries/block_buffer`
- `src/libraries/cast`
- `src/libraries/chrono`
- `src/libraries/command_options`
- `src/libraries/const_string`
- `src/libraries/csv`
- `src/libraries/error_handling`
- `src/libraries/filesystem`
- `src/libraries/googletest` / `googletest_main`
- `src/libraries/inplace_vector`
- `src/libraries/math`
- `src/libraries/memory`
- `src/libraries/pe`
- `src/libraries/pil`
- `src/libraries/Platform-Adaptive`
- `src/libraries/pool`
- `src/libraries/print`
- `src/libraries/puddle`
- `src/libraries/random`
- `src/libraries/rfc3339_clock`
- `src/libraries/sstring`
- `src/libraries/string_buffer`
- `src/libraries/strings`
- `src/libraries/test_data`
- `src/libraries/thread_description`
- `src/libraries/threadpool`
- `src/libraries/wrappers`
- `src/mc`
- `src/tools`
- `src/Linux/libraries`
