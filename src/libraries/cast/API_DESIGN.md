# Cast Library API Design

This document explains the design philosophy and usage patterns for the `m` cast library.

## API Hierarchy

The cast library provides three APIs with distinct purposes:

### 1. `m::to<>()` - Primary Runtime-Checked API ✅ **RECOMMENDED**

**Purpose**: Idiomatic, concise runtime-checked conversions with overflow detection.

**Usage**:
```cpp
#include <m/cast/to.h>

auto x = m::to<int32_t>(some_int64);     // Throws if overflow
auto y = m::to<uint8_t>(256);            // Throws std::overflow_error
auto z = m::to<int16_t>(-500);           // Succeeds
```

**Characteristics**:
- ✅ Concise and idiomatic (similar to Rust's `.into()`)
- ✅ Runtime overflow checking
- ✅ Clear exception messages with actual values
- ✅ Works with integrals, enums, pointers
- ✅ `constexpr` when possible
- ⚠️ Throws `std::overflow_error` on failure

**When to use**:
- Any time you need a checked conversion
- Default choice for numeric type conversions
- Replacing `static_cast` with safety guarantees

**Design rationale**: 
- Short name encourages adoption
- Reads naturally: "convert *to* type T"
- Implementation delegates to `try_cast` but provides better UX

---

### 2. `m::cast<>()` - Compile-Time Safe Conversions

**Purpose**: Zero-overhead conversions that are *provably* safe at compile time.

**Usage**:
```cpp
#include <m/cast/cast.h>

int16_t small = 42;
auto x = m::cast<int32_t>(small);   // OK: int16 → int32 always safe
auto y = m::cast<int64_t>(small);   // OK: widening conversion

// These will NOT compile:
// auto z = m::cast<int16_t>(large_int64);  // Compile error with helpful message
// auto w = m::cast<uint8_t>(some_int);     // Compile error
```

**Characteristics**:
- ✅ Compile-time only (no runtime overhead)
- ✅ Only accepts conversions that *cannot* overflow
- ✅ Clear compile error when unsafe
- ✅ `static_assert` directs users to `m::to<>()`
- ❌ Very limited scope (same-signedness widening only)

**When to use**:
- Hot paths where performance is critical
- When conversion is *statically* guaranteed safe
- When you want compile-time enforcement

**Design rationale**:
- Provides compile-time safety guarantees
- Name mirrors `static_cast` but with safety
- Intentionally limited to prevent misuse

---

### 3. `m::try_cast<>()` - Legacy Compatibility Wrapper ⚠️ **DEPRECATED FOR NEW CODE**

**Purpose**: Backward compatibility wrapper around `m::to<>()`.

**Status**: 
- Legacy API maintained for existing code
- Now a thin wrapper that forwards to `m::to<>()`
- Not recommended for new code
- May be formally deprecated in future

**Implementation**:
```cpp
// In try_cast.h - just forwards to m::to<>()
template <typename ToType, typename FromType>
constexpr decltype(auto)
try_cast(FromType const& from)
{
    return m::to<ToType>(from);
}
```

**Why it exists**:
- Historical: was the original implementation before `m::to<>()` was standardized
- Wordy name compared to idiomatic `m::to<>()`
- Maintained only for backward compatibility
- All logic has been moved to `m::to<>()`

**Recommendation**: Always use `m::to<>()` in new code. Update existing `try_cast` calls when touching code.

---

## Quick Reference

| API | Use Case | Runtime Check | Compile-Time Check | Overhead | Status |
|-----|----------|---------------|-------------------|----------|--------|
| `m::to<>()` ✅ | General purpose | Yes | No | Minimal* | **PRIMARY** |
| `m::cast<>()` | Provably safe | No | Yes | Zero | Limited scope |
| `m::try_cast<>()` ⚠️ | Legacy only | Yes | No | Minimal* | **DEPRECATED** |

\* Zero overhead when optimized if value is in range

## Migration from `static_cast`

### Before:
```cpp
// Dangerous - silent truncation
auto x = static_cast<int32_t>(some_size_t);
auto y = static_cast<uint8_t>(some_int);
```

### After:
```cpp
// Safe - throws on overflow
auto x = m::to<int32_t>(some_size_t);
auto y = m::to<uint8_t>(some_int);
```

## Exception Handling

All runtime-checked casts throw `std::overflow_error` with detailed messages:

```cpp
try {
    auto x = m::to<uint8_t>(300);
} catch (std::overflow_error const& e) {
    // Message: "try_cast overflow: value 300 exceeds maximum 255 for target type"
    std::cerr << e.what() << '\n';
}
```

## Performance

Both `m::to<>()` and `m::cast<>()` are designed for zero overhead:

- Marked `constexpr` - can be evaluated at compile time
- Inlined aggressively by compilers
- When value is in range, optimizes to same code as `static_cast`
- Only pays cost when overflow actually occurs (exception path)

**Benchmark results**: (TODO: Add once benchmarks implemented - see CHECKLIST.md #23)

## Future Directions

### C++23 `std::expected` Support (Potential)

For code that cannot use exceptions:
```cpp
// Potential future API
auto result = m::to_nothrow<int32_t>(some_value);
if (result.has_value()) {
    use(result.value());
} else {
    // Handle error
}
```

See CHECKLIST.md #16 for details.

### Custom Error Handlers (Potential)

```cpp
// Potential future API
auto x = m::to<int32_t>(value, [](auto v) {
    log_error("Overflow: {}", v);
    return default_value;
});
```

See CHECKLIST.md #15 for details.

## Summary

| When you need... | Use... |
|-----------------|--------|
| Safe numeric conversion | `m::to<>()` ✅ |
| Compile-time guarantee | `m::cast<>()` |
| Enum to integral | `m::to<>()` |
| Pointer downcast | `m::to<>()` |
| Replace `static_cast` | `m::to<>()` |

**Rule of thumb**: Use `m::to<>()` by default. Use `m::cast<>()` only when you need compile-time enforcement and the conversion is provably safe.

## Architecture

### Implementation Structure

The cast library has been refactored for clarity:

```
to.h          - Primary implementation (to_helper struct + m::to<T>())
try_cast.h    - Thin wrapper for compatibility (forwards to m::to<T>())
cast.h        - Compile-time safe casts (m::cast<T>())
```

**Key design decisions**:
- `m::to<>()` contains all the runtime checking logic in `to_helper`
- `m::try_cast<>()` is literally: `return m::to<ToType>(from);`
- `try_cast_helper` is a type alias: `using try_cast_helper = to_helper;`
- This puts the implementation in the file with the preferred API name
- Maintains 100% backward compatibility for existing code
