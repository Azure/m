# Fix for Issue #2: BUGGY Signed + Unsigned Operations

This document describes the fix that needs to be applied to `src/libraries/math/include/m/math/math.h` for the signed + unsigned -> unsigned operations (lines 556-596).

## Location
File: `src/libraries/math/include/m/math/math.h`
Lines: ~563-596 (the `safe_math_helper<signed, unsigned, unsigned>` specialization)

## Problem
The current implementation has two BUGGY operations:
1. **Addition**: The check `if ((rv < l) || (rv < r) || ...)` is meaningless when `l` is negative (comparing signed to result)
2. **Subtraction**: The check `if (r > l)` fails to catch all overflow cases and the subsequent logic is also buggy

## Solution

Replace the entire body of the `safe_math_helper<LeftT, RightT, ResultT>` struct (where LeftT=signed, RightT=unsigned, ResultT=unsigned) with:

```cpp
struct safe_math_helper<LeftT, RightT, ResultT>
{
    static constexpr ResultT
    add(LeftT l, RightT r)
    {
        //
        // Adding signed + unsigned with unsigned result.
        // If l is negative, the mathematical result is negative or zero,
        // which can only be represented in unsigned if the result is exactly zero.
        //
        // If l is non-negative, we can safely cast it to unsigned and perform
        // unsigned + unsigned addition with overflow checking.
        //
        
        if (l < 0)
        {
            // l is negative, r is unsigned.
            // The mathematical result is r + l where l < 0.
            // This is effectively r - |l|.
            //
            // Handle the special case where l is the most negative value
            if (l == (std::numeric_limits<LeftT>::min)())
            {
                // We can't safely negate this value, so we need special handling
                // Result = r - |min|
                // This can only succeed if r >= |min|
                constexpr uintmax_t abs_min = 
                    static_cast<uintmax_t>(-(static_cast<intmax_t>(
                        (std::numeric_limits<LeftT>::min)()) + 1)) + 1;
                
                auto promoted_r = static_cast<uintmax_t>(r);
                
                if (promoted_r < abs_min)
                {
                    throw std::overflow_error("integer overflow");
                }
                
                return m::try_cast<ResultT>(promoted_r - abs_min);
            }
            
            // l is negative but not the most negative value, so we can negate it
            auto abs_l = static_cast<uintmax_t>(-static_cast<intmax_t>(l));
            auto promoted_r = static_cast<uintmax_t>(r);
            
            if (promoted_r < abs_l)
            {
                // Result would be negative
                throw std::overflow_error("integer overflow");
            }
            
            return m::try_cast<ResultT>(promoted_r - abs_l);
        }
        
        // l is non-negative, so we can treat this as unsigned + unsigned
        auto l_as_unsigned = static_cast<uintmax_t>(l);
        auto promoted_r = static_cast<uintmax_t>(r);
        
        auto sum = l_as_unsigned + promoted_r;
        
        // Check for unsigned overflow
        if (sum < l_as_unsigned || sum < promoted_r)
        {
            throw std::overflow_error("integer overflow");
        }
        
        return m::try_cast<ResultT>(sum);
    }

    static constexpr ResultT
    subtract(LeftT l, RightT r)
    {
        //
        // Subtracting unsigned from signed with unsigned result.
        // Mathematical result is l - r.
        // This must be non-negative to fit in an unsigned type.
        //
        
        if (l < 0)
        {
            // l is negative, so l - r is definitely negative
            throw std::overflow_error("integer overflow");
        }
        
        // l is non-negative
        auto l_as_unsigned = static_cast<uintmax_t>(l);
        auto promoted_r = static_cast<uintmax_t>(r);
        
        if (l_as_unsigned < promoted_r)
        {
            // Result would be negative
            throw std::overflow_error("integer overflow");
        }
        
        auto diff = l_as_unsigned - promoted_r;
        
        return m::try_cast<ResultT>(diff);
    }
};
```

## Key Changes

### Addition (`add` function):
1. **Removed buggy common_type_t logic** - The old code promoted to a signed common type and then tried to check overflow with `rv < l` which is meaningless when l is negative
2. **Split into two cases**:
   - **Negative l**: Treat as `r - |l|` with proper handling for INT_MIN
   - **Non-negative l**: Treat as unsigned + unsigned addition
3. **Proper overflow detection**: Check if `r < |l|` before subtraction (would give negative result)

### Subtraction (`subtract` function):
1. **Removed buggy logic** - The old code only checked `if (r > l)` which compared signed with unsigned incorrectly
2. **Simplified logic**:
   - If `l < 0`, the result is always negative, so throw immediately
   - If `l >= 0`, convert both to unsigned and check if `l_as_unsigned < promoted_r`
3. **Proper overflow detection**: Catches all cases where result would be negative

## Testing
Comprehensive tests have been added in `src/libraries/math/test/signed_unsigned_to_unsigned.cpp` covering:
- Basic addition and subtraction
- Negative signed values
- INT_MIN edge cases
- Overflow detection
- Different sized types
- Narrowing conversions

## Manual Application
If the automated replacement fails, manually:
1. Open `src/libraries/math/include/m/math/math.h`
2. Find the specialization starting at line ~559: `struct safe_math_helper<LeftT, RightT, ResultT>` where the requires clause has `std::is_signed_v<LeftT> && std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>`
3. Remove everything from the opening `{` on line ~564 to the closing `};` on line ~596
4. Replace with the code above
5. Save the file
