# src/include Code Review Checklist

This checklist contains recommendations for improving the code in the `src/include` directory, which contains repository-wide header files (Layer 0 foundation).

## Critical Issues

### 1. Bug in `pointers.h` - swap function (Line 67)
- **File**: `src/include/m/utility/pointers.h`
- **Issue**: In `not_null<T>::swap()`, line 67 has `r.m_v = r;` which should be `r.m_v = t;`
- **Impact**: HIGH - This is a logic error that breaks the swap operation
- **Fix**:
  ```cpp
  // Current (WRONG):
  r.m_v = r;
  
  // Should be:
  r.m_v = t;
  ```

### 2. Missing Return Statement in `unique_unlock.h`
- **File**: `src/include/m/utility/unique_unlock.h`
- **Issue**: Line 69, `operator=(unique_unlock&&)` doesn't return `*this`
- **Impact**: HIGH - Violates move assignment operator contract
- **Fix**: Add `return *this;` at the end of the operator

### 3. Incorrect File Extension
- **File**: `src/include/m/utility/enum_operations.h.h`
- **Issue**: Double `.h.h` extension
- **Impact**: MEDIUM - Confusing and non-standard
- **Recommendation**: Rename to `enum_operations.h`

## High Priority Issues

### 4. Missing std:: Qualification in `make_span.h` (Line 32)
- **File**: `src/include/m/utility/make_span.h`
- **Issue**: Line 32 uses `span<T, N>` without `std::` prefix
- **Current**: `return span<T, N>(arr);`
- **Should be**: `return std::span<T, N>(arr);`
- **Impact**: MEDIUM - May not compile depending on ADL

### 5. Missing Noexcept on Move Assignment in `unique_unlock.h`
- **File**: `src/include/m/utility/unique_unlock.h`
- **Issue**: Move assignment operator (line 66) should be noexcept
- **Recommendation**: Add `noexcept` specifier

### 6. Incomplete Implementation in `unique_unlock.h`
- **File**: `src/include/m/utility/unique_unlock.h`
- **Issue**: The `release()` function at line 99 is truncated/incomplete
- **Recommendation**: Complete the implementation or verify it's not cut off

### 7. Missing Noexcept Specifier on `not_null` Copy Assignment
- **File**: `src/include/m/utility/pointers.h`
- **Issue**: Line 47, copy assignment operator should be noexcept (member is already noexcept)
- **Recommendation**: Add `noexcept` to match copy constructor

## Medium Priority Issues

### 8. Inconsistent Include Guard in `exception.h`
- **File**: `src/include/m/exception/exception.h`
- **Issue**: File only includes `m/utility/exception.h` - appears to be a forwarding header
- **Question**: Is this intentional? Should this file exist at all if it only forwards?
- **Recommendation**: Either document the forwarding purpose or consolidate

### 9. Verbose Trait Implementations in `stringish.h`
- **File**: `src/include/m/utility/stringish.h`
- **Issue**: Lines 55-120+ use verbose `std::enable_if_t` and multiple specializations
- **Recommendation**: Consider using C++20 concepts and `requires` clauses for cleaner code
- **Example**:
  ```cpp
  // Instead of enable_if_t specializations:
  template <typename T>
    requires std::same_as<remove_cvref_t<T>, std::basic_string<char>>
  struct stringish_char_type<T> { using type = char; };
  ```

### 10. Redundant Templated Destructor Comment
- **File**: `src/include/m/utility/error_macros.h`
- **Issue**: Lines 113-114, explicit reset with comment "give debuggers a place to set breakpoint"
- **Observation**: While useful, this pattern appears twice - consider if both are necessary
- **Recommendation**: Verify both call sites need this pattern or consolidate

### 11. Missing Documentation
- **Files**: All files in `src/include`
- **Issue**: Most utility headers lack comprehensive doxygen-style documentation
- **Missing**:
  - File-level documentation explaining purpose
  - Class/function documentation for public APIs
  - Usage examples
  - Parameter descriptions
- **Priority**: MEDIUM (header-only utilities used repository-wide should be well-documented)

### 12. type_traits.h Uses Old-Style Trait Definitions
- **File**: `src/include/m/utility/type_traits.h`
- **Issue**: Lines 14-28 use SFINAE-style traits instead of C++20 concepts
- **Recommendation**: Consider refactoring to use concepts where applicable
- **Example**:
  ```cpp
  // Current:
  template <typename T, typename enabled = void>
  struct is_integral_non_bool { static constexpr bool value = false; };
  
  // C++20:
  template <typename T>
  concept integral_non_bool = std::integral<T> && !std::same_as<T, bool>;
  ```

### 13. Missing `reset()` Implementation Details in `unique_unlock.h`
- **File**: `src/include/m/utility/unique_unlock.h`
- **Issue**: `reset()` is called in destructor but implementation not visible in excerpt
- **Recommendation**: Ensure `reset()` properly re-locks if armed

## Low Priority / Code Quality Issues

### 14. Inconsistent Macro Naming
- **File**: `src/include/m/utility/utility.h`
- **Issue**: Macros use `M_` prefix but are quite generic names
- **Examples**: `M_INTEGER_RELATIONAL_OPERATORS`, `M_INTEGER_OPERATIONS_INC_DEC`
- **Consideration**: These are invasive macros - ensure documentation warns about usage
- **Recommendation**: Add usage documentation and examples

### 15. Potential Misuse of `reinterpret_cast` in `byte_span.h`
- **File**: `src/include/m/utility/byte_span.h`
- **Issue**: Lines 32, 43, 52 use `reinterpret_cast<T*>` without alignment checks
- **Risk**: Undefined behavior if byte span is not properly aligned for type T
- **Recommendation**: 
  - Add static assertion or runtime check for alignment
  - Document alignment requirements
  - Consider using `std::bit_cast` (C++20) where appropriate

### 16. Exception Hierarchy Verbosity
- **File**: `src/include/m/utility/exception.h`
- **Issue**: Lines 50-180+ have repetitive exception class definitions
- **Observation**: Each exception type has identical boilerplate
- **Recommendation**: Consider using a macro or template base to reduce repetition
- **Example**:
  ```cpp
  #define M_DEFINE_EXCEPTION(ExceptionName, BaseClass) \
      class ExceptionName : public BaseClass { \
      public: \
          using BaseClass::BaseClass; \
          /* ... common code ... */ \
      };
  ```

### 17. Magic Number in Utility Macros
- **File**: `src/include/m/utility/utility.h`
- **Issue**: Line 65 uses `m::math::add(..., 1, ...)` with magic number 1
- **Observation**: This is acceptable in increment/decrement operators but worth noting
- **Recommendation**: No change needed, but document that these macros require math library

### 18. Global Mutable State
- **File**: `src/include/m/utility/error_macros.h`
- **Issue**: Line 227 declares `inline static global_error_list gs_global_error_list;`
- **Concern**: Global mutable state with static initialization order issues
- **Recommendation**: Document initialization guarantees and thread-safety
- **Note**: Current implementation appears thread-safe with mutex, but document it

### 19. Incomplete Type Trait in `type_traits.h`
- **File**: `src/include/m/utility/type_traits.h`
- **Issue**: Line 102 has `remove_optional<T>::type` without `typename`
- **Should be**: `using remove_optional_t = typename remove_optional<T>::type;`
- **Impact**: LOW - May not compile in all contexts

### 20. Test File in Include Directory
- **File**: `src/include/test/test_unique_unlock.cpp`
- **Issue**: Test file located in include directory rather than dedicated test directory
- **Recommendation**: Move to appropriate test directory structure
- **Note**: Same for `src/include/m/utility/test/test_pointers.cpp`

## Architecture / Design Considerations

### 21. Consider C++23 `std::expected` for Error Handling
- **Files**: Exception hierarchy in `exception.h` and `error_macros.h`
- **Consideration**: Repository uses C++20/C++23
- **Recommendation**: Document when to use exceptions vs `std::expected`
- **Benefits**: 
  - Better performance for expected error cases
  - Clearer error propagation
  - Fits "fail-fast" philosophy mentioned in error_macros.h comments

### 22. Locked Tag Design Pattern
- **File**: `src/include/m/utility/locked.h`
- **Observation**: Very simple tag type for indicating lock ownership
- **Consideration**: Could this be extended to encode lock type in type system?
- **Recommendation**: Document usage patterns and when this should be preferred

### 23. optional Extension Design
- **File**: `src/include/m/optional/optional.h`
- **Observation**: Adds `match` pattern for std::optional
- **Consideration**: This is a monadic operation similar to `and_then`, `or_else`
- **Recommendation**: Consider adding other monadic operations for completeness
- **Reference**: C++23 added monadic operations to std::optional

### 24. zstring Design (GSL Compatibility)
- **File**: `src/include/m/utility/zstring.h`
- **Observation**: Declares intent to be compatible with GSL's basic_zstring
- **Issue**: Currently just type aliases to raw pointers - no null-termination checking
- **Consideration**: Should this provide stronger guarantees?
- **Recommendation**: Document that these are semantic markers only

## Consistency & Standards Compliance

### 25. Verify C++20/C++23 Feature Usage
- **Files**: All
- **Issue**: Repository targets C++20 with selective C++23
- **Recommendations**:
  - Audit for use of C++23 features and ensure they're necessary
  - Document which headers require C++23 vs C++20
  - Ensure fallbacks exist where appropriate

### 26. Header Include Dependencies
- **File**: `src/include/m/utility/compiler.h`
- **Observation**: Includes `<version>` which is C++20
- **Recommendation**: Verify all target compilers support this header
- **Note**: MSVC and Clang are both supported per compiler.h

### 27. Platform Detection Macros
- **File**: `src/include/m/utility/platform.h`
- **Issue**: Line 15, uses both `_WIN32 || WIN32`
- **Observation**: Redundant check (WIN32 implies _WIN32 in modern usage)
- **Recommendation**: Simplify to just `_WIN32` or document why both are needed

### 28. Concept Forward Declarations
- **File**: `src/include/m/utility/concepts.h`
- **Issue**: References `m::character` concept in algorithm.h and stringish.h
- **Observation**: Good use of concepts for constraint
- **Recommendation**: Ensure all concept usage is constexpr-friendly

## Documentation Priorities

### 29. Document Layer 0 Headers
- **Issue**: These are Layer 0 (foundation) headers per layering.instructions.md
- **Recommendation**: Add documentation explaining:
  - What makes these Layer 0
  - What depends on them
  - What they cannot depend on
  - Design principles (header-only, minimal dependencies)

### 30. Create Overview Documentation
- **Recommendation**: Add `src/include/README.md` explaining:
  - Purpose of this directory (repository-wide headers)
  - Namespace organization (m::)
  - Dependency rules
  - How to add new headers
  - Relationship to layer system

## Build System

### 31. CMakeLists.txt Review
- **Files**: `src/include/m/CMakeLists.txt`, `src/include/CMakeLists.txt`
- **Recommendation**: Verify these properly expose headers as interface libraries
- **Check**: Ensure no implementation files (.cpp) in these directories
- **Verify**: Include paths are correctly set up for consuming targets

## Additional Findings (2026-02-25 Review)

### 32. Bug: `mutex.h` — `with_lock` C++20 Fallback Fails to Compile
- **File**: `src/include/m/utility/mutex.h`
- **Issue**: In the `#else` (non-C++23) branch of `with_lock`, the `std::forward` call uses invalid syntax:
  ```cpp
  // Current (WRONG):
  return std::invoke<Fn, Args...>(std::forward<Fn>(f), std::forward(Args)(args)...);
  // std::forward requires an explicit template argument, but (Args) here is a cast/call expression.
  ```
- **Fix**:
  ```cpp
  return std::invoke(std::forward<Fn>(f), std::forward<Args>(args)...);
  ```
- **Impact**: HIGH — fails to compile on any C++20-only or non-C++23 path.

### 33. Bug: `smallest_size.h` — Off-by-One in Boundary Checks
- **File**: `src/include/m/utility/smallest_size.h`
- **Issue**: The template uses `N < max()` but to represent values in `[0, N]`, the correct predicate is `N <= max()`. As written, `smallest_size_t<255>` resolves to `uint16_t` instead of `uint8_t`. The same off-by-one applies to the `uint16_t` and `uint32_t` arms.
- **Fix**: Change all `<` comparisons to `<=`:
  ```cpp
  (N <= (std::numeric_limits<uint8_t>::max)()),
  // etc.
  ```
- **Impact**: HIGH — silently selects a larger type than necessary, and semantically incorrect.

### 34. Bug: `algorithm.h` — Not Self-Contained (Missing Includes)
- **File**: `src/include/m/utility/algorithm.h`
- **Issue**: Uses `basic_sstring<CharT>`, `not_null<CharT const*>`, and `m::character<CharT>` without including the headers that define them (`stringish.h`, `pointers.h`, `concepts.h`). Compiles only due to accidental include ordering at call sites.
- **Fix**: Add the missing direct includes to `algorithm.h`.
- **Impact**: MEDIUM — fragile; will break if include order at call sites changes.

### 35. Bug: `compiler.h` — Non-Standard C++23 Detection Value for MSVC
- **File**: `src/include/m/utility/compiler.h`
- **Issue**: `M_HAS_CXX23` for MSVC checks `_MSVC_LANG >= 202004L`. The ISO C++23 value is `202302L`, not `202004L`. This accidentally works as a proxy for "beyond C++20" but is semantically wrong. The Clang and GCC paths already use the correct `202302L`.
- **Fix**:
  ```cpp
  #if _MSVC_LANG >= 202302L
  #define M_HAS_CXX23 1
  ```
- **Impact**: MEDIUM — could activate C++23 paths prematurely or incorrectly.

### 36. Dead Code: `incrementer.h` — `naive_incrementer` Is Never Instantiated
- **File**: `src/include/m/utility/incrementer.h`
- **Issue**: `incrementer_base_chooser` selects `integral_incrementer<T>` when `T` is integral and `naive_incrementer<T>` otherwise, but `naive_incrementer` itself also carries `requires(std::integral<T>)`. Non-integral `T` therefore fails both arms; `naive_incrementer` is dead code.
- **Recommendation**: Either remove the `requires` constraint from `naive_incrementer` (if it is intended for non-integral types), or remove `naive_incrementer` entirely.
- **Impact**: LOW (dead code, not a runtime bug).

### 37. Dead Code: `utility.h` — `M_INTEGER_OPERATIONS_PLUS_MINUS___OLD` Macro
- **File**: `src/include/m/utility/utility.h`
- **Issue**: A large macro `M_INTEGER_OPERATIONS_PLUS_MINUS___OLD` is present and clearly superseded by `M_INTEGER_OPERATIONS_PLUS_MINUS`. The `___OLD` suffix signals it is stale.
- **Recommendation**: Remove it.
- **Impact**: LOW (dead macro, zero runtime effect, but adds noise and confusion).

### 38. Minor: `type_traits.h` — Reimplements `std::remove_cvref_t`
- **File**: `src/include/m/utility/type_traits.h`
- **Issue**: `m::remove_cvref_t<T>` is manually defined as a chain of `remove_const_t`, `remove_volatile_t`, `remove_reference_t`. The library already requires C++20, which provides `std::remove_cvref_t<T>` directly.
- **Recommendation**:
  ```cpp
  template <typename T>
  using remove_cvref_t = std::remove_cvref_t<T>;
  ```
- **Impact**: LOW — only a maintenance burden; semantically identical.

### 39. Minor: `zstring.h` — `Extent` Template Parameter Is Silently Ignored
- **File**: `src/include/m/utility/zstring.h`
- **Issue**: `basic_zstring<CharT, Extent>` is defined as `CharT*` regardless of the `Extent` argument. The parameter is accepted but fully discarded, which is misleading to callers expecting span-like bounded behaviour.
- **Recommendation**: Either remove the `Extent` parameter, or add a static_assert/comment making clear it has no effect.
- **Impact**: LOW — misleading API, no runtime consequence.

### 40. Minor: `string_inserter.h` — Non-Idiomatic `[[nodiscard]]` on Output Iterator `operator*()`
- **File**: `src/include/m/utility/string_inserter.h`
- **Issue**: `[[nodiscard]]` is applied to `basic_string_insert_iterator::operator*()`. Standard output iterators (e.g., `std::back_insert_iterator`) do not mark this operator `[[nodiscard]]`, and doing so may generate spurious warnings in algorithm or range usage where the result of `operator*()` is immediately assigned through.
- **Recommendation**: Remove `[[nodiscard]]` from `operator*()`.
- **Impact**: LOW — may cause spurious `-Wunused-result` / nodiscard warnings.

### 41. Bug: `error_macros.h` — Infinite Loop in `unregister_handler` When Token Not Found
- **File**: `src/include/m/utility/error_macros.h`
- **Issue**: The `while` loop in `global_error_list::unregister_handler()` never increments the iterator when the current element does not match, causing an infinite loop if the token is not present in the deque:
  ```cpp
  while (it != end)
  {
      if (*it == e)
      {
          m_deque.erase(it);
          break;
      }
      // missing ++it — loops forever if e is not in m_deque
  }
  ```
- **Fix**: Add `++it;` at the end of the loop body.
- **Impact**: CRITICAL — denial of service / hang if `unregister_handler` is ever called with a token that is not in the list (e.g. double-unregister, or any future code path that calls it defensively).
- **Status**: ~~**FIXED**~~ — `++it;` added.

## Summary Statistics

- **Total Items**: 40
- **Critical**: 3 (bugs that must be fixed)
- **High Priority**: 6
- **Medium Priority**: 11
- **Low Priority**: 13
- **Architecture**: 4
- **Documentation**: 2
- **Build**: 1

## Priority Action Items

1. ~~**IMMEDIATE**: Fix swap bug in `pointers.h` line 67~~ — **VERIFIED CORRECT**, code already has `r.m_v = t;`
2. ~~**IMMEDIATE**: Fix missing return in `unique_unlock.h` line 69~~ — **VERIFIED CORRECT**, `return *this;` and `noexcept` already present
3. ~~**IMMEDIATE**: Fix incorrect type trait alias in `type_traits.h` line 102~~ — **VERIFIED CORRECT**, `typename` already present
4. ~~**IMMEDIATE**: Fix `with_lock` C++20 fallback syntax error in `mutex.h` (item 32)~~ — **FIXED**: corrected `std::forward(Args)(args)...` to `std::forward<Args>(args)...`
5. ~~**IMMEDIATE**: Fix off-by-one boundary checks in `smallest_size.h` (item 33)~~ — **FIXED**: changed all `<` to `<=` in boundary comparisons
6. ~~**HIGH**: Fix missing includes in `algorithm.h` to make it self-contained (item 34)~~ — **FIXED**: added `#include`s for `concepts.h`, `pointers.h`, `stringish.h`, `<optional>`, `<string>`, `<string_view>`
7. ~~**HIGH**: Fix MSVC C++23 detection value in `compiler.h` (item 35)~~ — **FIXED**: changed `_MSVC_LANG >= 202004L` to `_MSVC_LANG >= 202302L`
8. ~~**HIGH**: Add missing `std::` qualification in `make_span.h`~~ — **VERIFIED CORRECT**, all `std::span` uses already qualified
9. ~~**HIGH**: Rename `enum_operations.h.h` to remove double extension~~ — **VERIFIED CORRECT**, file is already `enum_operations.h`
10. **MEDIUM**: Add comprehensive documentation to all public APIs
11. **MEDIUM**: Consider modernizing trait implementations with concepts
12. **LOW**: Remove dead code: `naive_incrementer`, `M_INTEGER_OPERATIONS_PLUS_MINUS___OLD`, phantom `Extent` in `zstring.h`
13. ~~**IMMEDIATE**: Fix infinite loop in `error_macros.h` `unregister_handler` (item 41)~~ — **FIXED**: added missing `++it;` in while loop

---

## Notes

This checklist focuses on the header-only utilities that form the foundation layer of the repository. These headers are critical as they're used throughout the codebase, so quality and correctness are paramount.

The code is generally well-structured and makes good use of modern C++ features. The main areas for improvement are:
1. Fixing the identified bugs
2. Adding comprehensive documentation
3. Modernizing some older C++11/14 patterns to C++20 concepts
4. Ensuring consistent error handling patterns

All recommendations align with the repository's coding standards:
- Memory safety
- Avoiding C-style casts (mostly adhered to)
- Clean builds for debug and retail
- Appropriate use of C++20/C++23 features
