# Cast Library Code Review Checklist

This checklist contains recommendations for improving the `src/libraries/cast` library, which provides safe casting operations (Layer 0 foundation).

## High Priority Issues

### 1. Incomplete Implementations in `try_cast.h`
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Lines 175-178 declare but don't implement chrono duration/time_point casting
  ```cpp
  // Enable casting from std::chrono::duration
  template <typename Rep, typename Period, typename ToType>
      requires(std::integral<Rep>)
  struct try_cast_helper<std::chrono::duration<Rep, Period>, ToType, void>;

  // Enable casting from std::chrono::time_point
  template <typename Clock, typename Duration, typename ToType>
  struct try_cast_helper<std::chrono::time_point<Clock, Duration>, ToType, void>;
  ```
- **Impact**: HIGH - Forward declarations without implementations will cause linker errors if used
- **Recommendation**: Either:
  - Implement these specializations with proper overflow checking
  - Remove declarations if not yet ready
  - Document as "planned but not yet implemented"

### 2. ~~Incomplete `cast.h` Implementation~~ [RESOLVED - BY DESIGN]
- **File**: `src/libraries/cast/include/m/cast/cast.h`
- **Status**: Only implements one specialization (line 69-79) for same-signedness widening casts
- **Design Decision**: This is **intentional** - `m::cast<>()` should only compile for statically-safe conversions
  - Same-signedness widening casts (e.g., `int16_t` → `int32_t`, `uint8_t` → `uint16_t`)
  - Unsafe narrowing conversions intentionally fail to compile
  - Added `static_assert` for better error messages directing users to `m::to<>()`
- **API Guidance**:
  - Use `m::cast<>()` when you KNOW the conversion is safe at compile time
  - Use `m::to<>()` for runtime-checked conversions with overflow detection ✅ **RECOMMENDED**

### 3. ~~Generic Exception Messages~~ [RESOLVED]
- **File**: `src/libraries/cast/include/m/cast/to.h` (implementation now here)
- **Status**: All exceptions now use `std::format` with detailed context
- **Implementation**: Exception messages now include:
  - The actual value that caused the overflow
  - The target type's min/max limits (where applicable)
  - Clear description of the problem (negative to unsigned, exceeds max, below min)
  - For pointer casts: type names of source and target types
  - Messages now say "m::to overflow" (primary API)
- **Examples**:
  - `"m::to overflow: value 300 exceeds maximum 127 for target type"`
  - `"m::to overflow: negative value -5 cannot be converted to unsigned type"`
  - `"m::to failed: unable to safely downcast pointer from Base to Derived"`
- **Note**: Uses `<format>` which is C++20 standard library
- **Refactoring**: Logic moved from `try_cast.h` to `to.h` as `to_helper`

### 4. ~~Empty Implementation File~~ [RESOLVED] (Related to #19, #30)
- **File**: ~~`src/libraries/cast/src/cast.cpp`~~ (removed)
- **Status**: ✅ **COMPLETED** - File has been removed
- **Implementation**: 
  - Removed empty cast.cpp file
  - Converted library to INTERFACE (header-only) in CMakeLists.txt
  - Updated all PUBLIC keywords to INTERFACE
  - Added include directories and dependencies to main CMakeLists.txt
- **Client Code Impact**: ✅ **ZERO** - No client changes needed, syntax remains identical

## Medium Priority Issues

### 5. ~~Inconsistent API Naming~~ [RESOLVED - BY DESIGN]
- **Files**: `cast.h`, `try_cast.h`, `to.h`
- **Status**: ✅ **REFACTORED** - Clear API hierarchy established
- **Implementation Changes**:
  - `m::to<T>()` - **PRIMARY API** contains all implementation logic (in `to.h`)
  - `m::try_cast<T>()` - **THIN WRAPPER** that forwards to `m::to<T>()` (legacy compatibility)
  - `m::cast<T>()` - compile-time safe casts only (intentionally limited)
- **Design Decision**: 
  - `m::to<>()` is the preferred public API for runtime-checked conversions
  - `m::try_cast<>()` maintained for backward compatibility but discouraged for new code
  - `to_helper` struct contains implementation (renamed from `try_cast_helper`)
  - `try_cast_helper` is now a `using` alias to `to_helper` for compatibility
  - `m::to<>()` provides a concise, idiomatic interface (similar to Rust's `.into()`)
- **API Guidance**:
  - Use `m::cast<>()` when conversion is statically guaranteed safe
  - Use `m::to<>()` for all runtime-checked conversions ✅ **RECOMMENDED**
  - Avoid using `m::try_cast<>()` directly in new code (legacy compatibility only)

### 6. Floating Point Cast Design Decision Not Resolved
- **File**: `src/libraries/cast/include/m/cast/to.h`
- **Issue**: Lines 44-65 discuss floating point conversions but conclude with uncertainty
- **Quote**: "[MicGrier: I'd prefer to simply avoid floating point or only allow T -> T conversions?]"
- **Impact**: MEDIUM - Floating point casts are undefined/unimplemented
- **Recommendation**: 
  - Make a decision on floating point support
  - Either implement with clear semantics or explicitly disable via concept constraints
  - Document the decision in the header

### 7. Commented Out Code in Tests
- **File**: `src/libraries/cast/test/test_try_cast.cpp`
- **Issue**: Lines 24-28 have commented out variable declarations
  ```cpp
  // auto const r_least     = m::try_cast<T>(least);
  // auto const r_greatest  = m::try_cast<T>(greatest);
  // auto const r_minus_one = m::try_cast<T>(minus_one);
  // auto const r_zero      = m::try_cast<T>(zero);
  // auto const r_one       = m::try_cast<T>(one);
  ```
- **Impact**: LOW - Confusing, unclear why commented
- **Recommendation**: Remove commented code or document why it's there

### 8. Limited Test Coverage
- **Files**: `test/test_cast.cpp`, `test/test_try_cast.cpp`, `test/to.cpp`
- **Missing Tests**:
  - Cross-signedness conversions (signed ↔ unsigned)
  - Cross-size conversions (int8 ↔ int16 ↔ int32 ↔ int64)
  - Edge cases near boundaries
  - Enum to integral conversions
  - Pointer downcasting
  - Negative test cases (expected failures)
- **Impact**: MEDIUM - Can't verify correctness across all scenarios
- **Recommendation**: Add comprehensive test matrix covering:
  ```
  - int8_t  → int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t
  - uint8_t → int8_t, int16_t, int32_t, int64_t, uint16_t, uint32_t, uint64_t
  - ... (complete matrix)
  - Boundary values (min, max, max+1, min-1)
  - Zero and near-zero values
  - Pointer hierarchy tests
  ```

### 9. ~~No Documentation for When to Use Which API~~ [RESOLVED]
- **Files**: All header files
- **Status**: API usage guidance has been clarified
- **Design Decision**: Clear hierarchy established:
  1. **`m::to<>()`** - Primary API for runtime-checked conversions (concise, idiomatic) ✅
  2. **`m::cast<>()`** - Compile-time safe conversions only (limited scope)
  3. **`m::try_cast<>()`** - Implementation detail (wordy, not for direct use)
- **Documentation Needed**: Add to headers:
  - Performance characteristics (zero-overhead when optimized)
  - Examples showing `m::to<>()` usage patterns
  - Migration guide from `static_cast` to `m::to<>()`

## Low Priority / Code Quality Issues

### 10. Verbose Template Syntax
- **File**: `src/libraries/cast/include/m/cast/cast.h`
- **Issue**: Lines 91-96 use old-style SFINAE with `enable_if_t`
  ```cpp
  template <typename ToType, typename FromType>
  struct is_castable<ToType,
                     FromType,
                     std::enable_if_t<cast_helper<FromType, ToType>::is_castable>>
  ```
- **Observation**: Could be modernized with C++20 concepts
- **Recommendation**: Consider refactoring to use concepts/requires for clarity:
  ```cpp
  template <typename ToType, typename FromType>
    requires cast_helper<FromType, ToType>::is_castable
  struct is_castable<ToType, FromType>
  ```

### 11. Deleted Constructors in Helper
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Lines 53-57 explicitly delete constructors in a stateless helper struct
  ```cpp
  try_cast_helper()                       = delete;
  try_cast_helper(try_cast_helper const&) = delete;
  try_cast_helper&
  operator=(try_cast_helper const&) = delete;
  ```
- **Observation**: Unnecessary since all members are static
- **Recommendation**: Remove deleted functions or make the struct a namespace with functions

### 12. Redundant `const&` on Integral Parameters
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Lines 80, 109, 132, 159 pass small integral types by `const&`
  ```cpp
  static constexpr decltype(auto)
  do_cast(FromType const& v)  // FromType is int8_t, int16_t, etc.
  ```
- **Impact**: LOW - Minimal, but inconsistent with best practices
- **Recommendation**: Pass small types by value for performance
  - For integrals: `do_cast(FromType v)`
  - Keep `const&` for larger types or when perfect forwarding is needed

### 13. Unnecessary Variable in Enum Cast
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Line 205 creates unnecessary copy
  ```cpp
  FromType   v1 = v;
  auto const t  = m::to_underlying(v1);
  ```
- **Should be**: `auto const t = m::to_underlying(v);`
- **Impact**: LOW - Compiler likely optimizes this away
- **Recommendation**: Remove intermediate variable

### 14. Inconsistent Return Type Declaration
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Some functions use `decltype(auto)` (line 80), others use explicit types (line 191)
- **Observation**: No clear pattern
- **Recommendation**: Be consistent - use explicit return types for clarity in this context

## Architecture / Design Considerations

### 15. Future Work Comment
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Lines 31-38 describe future extensibility with lambda for custom exceptions
- **Quote**: "Add try_cast() variant that can take a lambda that is std::invoke()d when the cast would not succeed"
- **Recommendation**: 
  - Track this as a feature request
  - Consider if `std::expected<T, ErrorType>` (C++23) would be better than exceptions
  - Document the extensibility plan

### 16. Consider `std::expected` for C++23
- **All Files**
- **Observation**: Library uses exceptions for failure cases
- **Consideration**: Repository targets C++20/C++23
- **Recommendation**: 
  - For C++23 builds, consider adding:
    ```cpp
    template <typename ToType, typename FromType>
    std::expected<ToType, std::error_code> 
    try_cast_nothrow(FromType const& from) noexcept;
    ```
  - This would provide zero-overhead error handling for performance-critical code
  - Keep exception-based API as default for ergonomics

### 17. Pointer Cast Safety
- **File**: `src/libraries/cast/include/m/cast/try_cast.h`
- **Issue**: Lines 181-193 use `dynamic_cast` for pointer downcasting
- **Concerns**:
  - Requires RTTI (may be disabled in some configurations)
  - Only works with polymorphic types
  - Runtime overhead
- **Recommendation**: 
  - Document RTTI requirement
  - Consider adding compile-time checks for polymorphic types
  - Add tests for non-polymorphic types (should fail to compile, not runtime)

### 18. No Support for Reference Casts
- **All Files**
- **Issue**: Only handles value and pointer types, not references
- **Example**: `int& x = ...; auto y = m::try_cast<long&>(x);` doesn't work
- **Consideration**: Is this intentional?
- **Recommendation**: Document whether reference casts are intentionally excluded

### 19. ~~Header-Only vs Library Design~~ [RESOLVED] (Related to #4, #30)
- **Files**: ~~`cast.cpp`~~ (removed), `CMakeLists.txt`
- **Status**: ✅ **COMPLETED** - Converted to INTERFACE library
- **Implementation**: 
  - Removed empty cast.cpp
  - Changed from STATIC to INTERFACE library
  - Updated all CMakeLists.txt files appropriately
- **Benefits Achieved**:
  - Faster builds (no compilation of empty translation unit)
  - More accurate representation of library nature
  - Cleaner build system

## Testing Gaps

### 20. No Negative Tests for `m::cast<>`
- **File**: `test/test_cast.cpp`
- **Issue**: No tests verifying that invalid casts don't compile
- **Impact**: MEDIUM - Can't verify safety guarantees
- **Recommendation**: Add compile-fail tests or static_assert tests:
  ```cpp
  // Should not compile:
  // static_assert(!m::is_castable_v<int8_t, int64_t>);
  ```

### 21. Insufficient Overflow Tests
- **File**: `test/test_try_cast.cpp`
- **Issue**: Only 2 overflow tests (lines 94-103)
- **Missing**:
  - Signed overflow (positive and negative)
  - Boundary +/- 1 tests for all type combinations
  - Large value tests
- **Recommendation**: Add systematic boundary testing

### 22. No Enum Tests in `test_cast.cpp`
- **File**: `test/test_cast.cpp`
- **Issue**: No enum casting tests, but `test/to.cpp` has one
- **Recommendation**: Add enum tests to both files for consistency

### 23. No Performance Benchmarks
- **All Files**
- **Issue**: No benchmarks comparing to `static_cast`
- **Recommendation**: Add benchmark tests to verify zero-overhead in optimized builds
- **Example**: 
  ```cpp
  // Verify try_cast compiles to same code as static_cast when in range
  benchmark_compare([] { 
      return m::try_cast<int>(42); 
  }, [] { 
      return static_cast<int>(42); 
  });
  ```

## Documentation Priorities

### 24. Missing Library Overview
- **Recommendation**: Add `src/libraries/cast/README.md` explaining:
  - Purpose: Safe alternatives to C++ casts
  - When to use this library vs standard casts
  - Performance characteristics
  - Examples of each API
  - Integration with other `m` library components

### 25. API Documentation Incomplete
- **Files**: All header files
- **Issue**: Good top-level comments but no per-function documentation
- **Recommendation**: Add doxygen-style documentation:
  ```cpp
  /// @brief Safely cast between integral types with runtime overflow checking
  /// @tparam ToType Target integral type
  /// @param from Source value
  /// @return Converted value if safe
  /// @throws std::overflow_error if value doesn't fit in ToType
  template <typename ToType, typename FromType>
  constexpr decltype(auto) try_cast(FromType const& from);
  ```

### 26. Examples Section Needed
- **Recommendation**: Add examples section showing:
  - Basic usage
  - Common pitfalls avoided
  - Integration with math library
  - Custom exception handling

## Consistency with Repository Standards

### 27. Verify Layer 0 Status
- **Issue**: Library depends on `m/utility/to_underlying.h` and `m/utility/type_traits.h`
- **Question**: Are these also Layer 0? (They should be per layering rules)
- **Recommendation**: Verify no circular dependencies exist

### 28. Exception Types
- **Issue**: Uses standard exceptions (`std::overflow_error`, `std::runtime_error`)
- **Observation**: Other parts of repo define custom exceptions (e.g., `m::invalid_parameter`)
- **Consideration**: Should this use `m::` exception types?
- **Recommendation**: Decide on exception hierarchy and document decision

## Build System

### 29. CMake Configuration
- **File**: `CMakeLists.txt`
- **Issue**: Very basic configuration
- **Missing**:
  - No warning level specified
  - No indication of header-only vs compiled
- **Recommendation**: Clarify build model and add appropriate flags

### 30. ~~Convert to Header-Only INTERFACE Library~~ [RESOLVED] (Related to #4, #19)
- **Files**: `CMakeLists.txt`, ~~`cast.cpp`~~ (removed)
- **Status**: ✅ **COMPLETED** - Successfully converted to INTERFACE library
- **Implementation**:
  ```cmake
  add_library(m_cast INTERFACE)
  target_compile_features(m_cast INTERFACE ${M_CXX_STD})
  target_sources(m_cast INTERFACE FILE_SET HEADERS FILES ...)
  target_include_directories(m_cast INTERFACE include)
  target_link_libraries(m_cast INTERFACE m_math)
  ```
- **Changes Made**:
  - Removed empty src/cast.cpp
  - Changed library type from STATIC to INTERFACE
  - Updated all PUBLIC to INTERFACE
  - Removed src subdirectory from build
  - Moved include directories and dependencies to main CMakeLists.txt
- **Client Code Impact**: ✅ **ZERO** - No client changes needed

## Summary Statistics

- **Total Items**: 30
- **Completed**: 8 ✅
- **High Priority**: 2 (1 remaining)
- **Medium Priority**: 5 (3 completed)
- **Low Priority**: 5
- **Architecture**: 5
- **Testing**: 4
- **Documentation**: 3
- **Consistency**: 2
- **Build**: 2 (both completed)

## Priority Action Items

1. **IMMEDIATE**: Remove or implement chrono cast forward declarations (item 1)
2. ~~**IMMEDIATE**: Clarify status of `m::cast<>()` - complete or deprecate (item 2)~~ ✅ DONE
3. **HIGH**: Add comprehensive test coverage for all type combinations (item 8)
4. ~~**HIGH**: Improve exception messages or add nothrow variants (item 3)~~ ✅ DONE
5. ~~**MEDIUM**: Resolve API naming inconsistency (item 5)~~ ✅ DONE - `m::to<>()` is primary API
6. **MEDIUM**: Resolve floating point cast design decision (item 6)
7. **MEDIUM**: Add library documentation (items 24-26) - ✅ API_DESIGN.md created
8. **LOW**: Clean up code quality issues (items 10-14)

---

## Notes

The cast library provides critical safety infrastructure for the repository. The core `try_cast` implementation appears solid but incomplete:

**Strengths**:
- Clean separation of signed/unsigned/pointer casting logic
- Good use of C++20 concepts
- Compile-time optimization via `constexpr`
- Addresses real safety problems with C++ casts

**Weaknesses**:
- Incomplete implementations (`cast.h`, chrono types)
- Limited test coverage
- Poor error messages for production debugging
- Unclear API boundaries (cast vs try_cast vs to)
- No documentation of intended usage patterns

**Recommendations align with repository standards**:
- Memory safety: ✅ (purpose of library)
- Avoiding C-style casts: ✅ (purpose of library)
- Clean builds: ⚠️ (forward declarations may cause issues)
- Unit tests: ⚠️ (basic tests exist but insufficient coverage)
