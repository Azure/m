import re

# Read the file
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Define the old code to replace (the BUGGY signed+unsigned implementation)
old_code = r'''        //
        // Handle \(signed \[op\] unsigned\) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        \{
            using common_type_t = std::common_type_t<LeftT, RightT>;

            // Should go without saying, but\.\.\.
            static_assert\(std::is_signed_v<common_type_t>\);

            static constexpr ResultT
            add\(LeftT l, RightT r\)
            \{
                common_type_t const rv =
                    static_cast<common_type_t>\(l\) \+ static_cast<common_type_t>\(r\);

                // BUGGY

                if \(\(rv < l\) \|\| \(rv < r\) \|\| \(rv > \(std::numeric_limits<ResultT>::max\(\)\)\)\)
                    throw std::overflow_error\("integer overflow"\);

                return m::try_cast<ResultT>\(rv\);
            \}

            static constexpr ResultT
            subtract\(LeftT l, RightT r\)
            \{
                if \(r > l\)
                    throw std::overflow_error\("integer overflow"\);

                // BUGGY

                auto const rv = static_cast<common_type_t>\(l\) - static_cast<common_type_t>\(r\);

                return m::try_cast<ResultT>\(rv\);
            \}
        \};'''

# Define the new code
new_code = '''        //
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
        };'''

# Try regex replacement
new_content = re.sub(old_code, new_code, content)

if new_content != content:
    # Write the modified content back
    with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("SUCCESS: File updated")
else:
    print("ERROR: Pattern not found")
