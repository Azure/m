#!/usr/bin/env python3
# Script to apply fix for Issue #2: BUGGY Signed + Unsigned Operations

# Read the file
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Find the section to replace
start_marker = '        //\n        // Handle (signed [op] unsigned) -> unsigned\n        //'
end_marker = '        //\n        // Handle (signed [op] signed) -> signed\n        //'

start_pos = content.find(start_marker)
end_pos = content.find(end_marker)

if start_pos == -1 or end_pos == -1:
    print("ERROR: Could not find markers")
    exit(1)

# Define the replacement code
new_section = '''        //
        // Handle (signed [op] unsigned) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>
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

'''

# Replace the section
new_content = content[:start_pos] + new_section + content[end_pos:]

# Write back
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(new_content)

print("SUCCESS: File updated")
print(f"Replaced {end_pos - start_pos} bytes with {len(new_section)} bytes")
