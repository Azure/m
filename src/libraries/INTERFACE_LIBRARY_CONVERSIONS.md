# INTERFACE Library Conversions

This document tracks the conversion of header-only libraries from STATIC to INTERFACE libraries.

## Rationale

Many libraries in the `m` repository are header-only but were built as STATIC libraries with empty .cpp files. This was required because CMake STATIC libraries must have at least one source file. However, this approach has drawbacks:
- Unnecessary compilation of empty translation units
- Misleading representation (library appears compiled when it's header-only)
- Wasted build time

Converting to INTERFACE libraries eliminates these issues while maintaining full backward compatibility with client code.

## Converted Libraries

The following libraries have been converted from STATIC to INTERFACE (2024):

### 1. **m_cast**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/cast/src/cast.cpp` (empty file with just includes)
- **Dependencies**: m_math
- **Changes**: 
  - Changed `add_library(m_cast STATIC)` → `INTERFACE`
  - Changed all `PUBLIC` → `INTERFACE`
  - Removed `add_subdirectory(src)`
  - Added `target_include_directories` and `target_link_libraries`

### 2. **m_atomic**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/atomic/src/atomic.cpp` (empty namespace)
- **Dependencies**: None
- **Changes**: Same pattern as m_cast

### 3. **m_bitset**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/bitset/src/bitset.cpp` (empty namespace)
- **Dependencies**: m_error_handling
- **Changes**: Same pattern as m_cast

### 4. **m_inplace_vector**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/inplace_vector/src/inplace_vector.cpp` (empty namespace)
- **Dependencies**: m_error_handling
- **Changes**: Same pattern as m_cast

### 5. **m_math**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/math/src/math.cpp` (just includes)
- **Dependencies**: m_cast
- **Changes**: Same pattern as m_cast

### 6. **m_pool**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/pool/src/pool.cpp` (empty namespace)
- **Dependencies**: m_bitset, m_error_handling, m_puddle
- **Changes**: Same pattern as m_cast

### 7. **m_print**
- **Status**: ✅ Converted
- **Removed**: `src/libraries/print/src/print.cpp` (empty namespace)
- **Dependencies**: m_error_handling
- **Changes**: Same pattern as m_cast

## Client Code Impact

✅ **ZERO** - No client code changes required!

The syntax `target_link_libraries(my_target PRIVATE m_library)` works identically for both STATIC and INTERFACE libraries. All existing client code continues to work without modification.

## Benefits

1. ✅ **Faster builds** - No compilation of empty translation units
2. ✅ **Cleaner codebase** - No more dummy .cpp files
3. ✅ **Accurate representation** - Library type matches actual implementation
4. ✅ **Zero client impact** - Full backward compatibility
5. ✅ **Future flexibility** - Can convert back to STATIC if implementation is added

## CMakeLists.txt Pattern

### Before (STATIC library):
```cmake
add_library(m_library STATIC)
target_compile_features(m_library PUBLIC ${M_CXX_STD})
add_subdirectory(include)
add_subdirectory(src)  # Contains empty .cpp
```

### After (INTERFACE library):
```cmake
add_library(m_library INTERFACE)
target_compile_features(m_library INTERFACE ${M_CXX_STD})
add_subdirectory(include)
# src subdirectory removed
target_include_directories(m_library INTERFACE include)
target_link_libraries(m_library INTERFACE dependencies...)
```

## Libraries NOT Converted

The following libraries have non-empty .cpp files and remain STATIC:

- **m_arefc_ptr** - Has actual implementation (189 bytes but contains code)
- **m_block_buffer** - Has actual implementation
- **m_command_options** - Has actual implementation
- **m_const_string** - Has actual implementation
- **m_debugging** - Has actual implementation (dbg_format.cpp)
- **m_error_handling** - Has actual implementation
- **m_googletest_main** - Has actual implementation (948 bytes)
- **m_io** - Has actual implementation
- **m_memory** - Has substantial implementation (1527 bytes)
- **m_puddle** - Has actual implementation
- **m_random** - Has actual implementation
- **m_rfc3339_clock** - Has actual implementation
- **m_rust-ffi-helpers** - Has actual implementation
- **m_sstring** - Has actual implementation
- **m_string_buffer** - Has actual implementation
- **m_test_data** - Has actual implementation
- **m_wrappers** - Has actual implementation

## Verification

To verify successful conversion:
```bash
# Check that empty .cpp files are removed
ls src/libraries/*/src/*.cpp | Select-String -Pattern "cast|atomic|bitset|inplace_vector|math|pool|print"

# Should return no results

# Check CMakeLists.txt shows INTERFACE
grep -r "add_library(m_cast" src/libraries/cast/CMakeLists.txt
# Should show: add_library(m_cast INTERFACE)
```

## References

- CMake documentation: [INTERFACE Libraries](https://cmake.org/cmake/help/latest/command/add_library.html#interface-libraries)
- Repository standard: `.github/instructions/cxx.instructions.md`
