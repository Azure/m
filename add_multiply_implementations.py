#!/usr/bin/env python3
# Script to add multiply() implementations to all safe_math_helper specializations

import re

# Read the file
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add multiply to unsigned + unsigned -> signed specialization
# Find where divide was just added
marker1 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Division of two unsigned values with signed result.
                // Use the unsigned/unsigned divide and cast to signed.
                return m::try_cast<ResultT>(Doppelganger::divide(l, r));
            }
        };

        //
        // Handle (unsigned [op] signed) -> unsigned'''

pos1 = content.find(marker1)
if pos1 != -1:
    # Insert multiply before divide
    insert_marker = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Division of two unsigned values with signed result.'''
    
    insert_pos = content.find(insert_marker, pos1)
    if insert_pos != -1:
        multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Multiplication of two unsigned values with signed result.
                // Use the unsigned/unsigned multiply and cast to signed.
                return m::try_cast<ResultT>(Doppelganger::multiply(l, r));
            }

            '''
        content = content[:insert_pos] + multiply_code + content[insert_pos:]
        print("Added multiply to unsigned×unsigned→signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 2. Add multiply to unsigned + signed -> unsigned specialization
marker2 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Unsigned / signed with unsigned result.
                
                if (r == 0)
                    throw std::overflow_error("integer overflow");'''

pos2 = content.find(marker2)
if pos2 != -1:
    multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Unsigned × signed with unsigned result.
                // If r is negative, result is negative (can't represent in unsigned).
                // If r is positive, perform unsigned × unsigned multiplication.
                
                if (r == 0 || l == 0)
                    return 0;
                
                if (r < 0)
                {
                    // Multiplying positive by negative gives negative result
                    throw std::overflow_error("integer overflow");
                }
                
                // r is positive, safe to cast to unsigned
                auto l_promoted = static_cast<uintmax_t>(l);
                auto r_as_unsigned = static_cast<uintmax_t>(r);
                
                auto prod = l_promoted * r_as_unsigned;
                
                // Check for overflow using division
                if ((prod / l_promoted) != r_as_unsigned || (prod / r_as_unsigned) != l_promoted)
                {
                    throw std::overflow_error("integer overflow");
                }
                
                return m::try_cast<ResultT>(prod);
            }

            '''
    content = content[:pos2] + multiply_code + content[pos2:]
    print("Added multiply to unsigned×signed→unsigned")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 3. Add multiply to signed + unsigned -> signed specialization
marker3 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Signed / unsigned with signed result.
                
                if (r == 0)
                    throw std::overflow_error("integer overflow");
            
                auto promoted_l = static_cast<intmax_t>(l);'''

pos3 = content.find(marker3)
if pos3 != -1:
    multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Signed × unsigned with signed result.
                
                if (l == 0 || r == 0)
                    return 0;
                
                auto promoted_l = static_cast<intmax_t>(l);
                auto promoted_r = static_cast<uintmax_t>(r);
                
                // Check for overflow by examining the magnitudes
                // The result magnitude is |l| × r
                
                if (promoted_l > 0)
                {
                    // Positive × unsigned: treat as unsigned multiplication
                    auto l_as_unsigned = static_cast<uintmax_t>(promoted_l);
                    auto prod = l_as_unsigned * promoted_r;
                    
                    // Check overflow
                    if (prod / l_as_unsigned != promoted_r || prod / promoted_r != l_as_unsigned)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    return m::try_cast<ResultT>(prod);
                }
                else
                {
                    // Negative × unsigned: result is negative
                    // Compute |l| × r, then negate
                    
                    // Handle INT_MIN specially
                    if (promoted_l == (std::numeric_limits<intmax_t>::min)())
                    {
                        // |INT_MIN| is INT_MAX + 1
                        constexpr uintmax_t abs_min =
                            static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                        
                        auto prod = abs_min * promoted_r;
                        
                        // Check overflow
                        if (prod / abs_min != promoted_r || prod / promoted_r != abs_min)
                        {
                            throw std::overflow_error("integer overflow");
                        }
                        
                        // Result is -(abs_min × r)
                        // Check if it fits in signed range (must be <= abs_min)
                        if (prod > abs_min)
                        {
                            throw std::overflow_error("integer overflow");
                        }
                        
                        if (prod == abs_min)
                        {
                            return (std::numeric_limits<ResultT>::min)();
                        }
                        
                        return m::try_cast<ResultT>(-static_cast<intmax_t>(prod));
                    }
                    
                    auto abs_l = static_cast<uintmax_t>(-promoted_l);
                    auto prod = abs_l * promoted_r;
                    
                    // Check overflow
                    if (prod / abs_l != promoted_r || prod / promoted_r != abs_l)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    // Negate and check it fits
                    constexpr uintmax_t max_negative =
                        static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                    
                    if (prod > max_negative)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    if (prod == max_negative)
                    {
                        return m::try_cast<ResultT>((std::numeric_limits<intmax_t>::min)());
                    }
                    
                    return m::try_cast<ResultT>(-static_cast<intmax_t>(prod));
                }
            }

            '''
    content = content[:pos3] + multiply_code + content[pos3:]
    print("Added multiply to signed×unsigned→signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 4. Add multiply to unsigned + signed -> signed specialization
marker4 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Unsigned / signed with signed result.
                
                if (r == 0)
                    throw std::overflow_error("integer overflow");
            
                if (r < 0)
                {
                    // Unsigned / negative = negative or zero'''

pos4 = content.find(marker4)
if pos4 != -1:
    multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Unsigned × signed with signed result.
                
                if (l == 0 || r == 0)
                    return 0;
                
                auto promoted_l = static_cast<uintmax_t>(l);
                auto promoted_r = static_cast<intmax_t>(r);
                
                if (promoted_r > 0)
                {
                    // Unsigned × positive: treat as unsigned multiplication
                    auto r_as_unsigned = static_cast<uintmax_t>(promoted_r);
                    auto prod = promoted_l * r_as_unsigned;
                    
                    // Check overflow
                    if (prod / promoted_l != r_as_unsigned || prod / r_as_unsigned != promoted_l)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    return m::try_cast<ResultT>(prod);
                }
                else
                {
                    // Unsigned × negative: result is negative
                    
                    // Handle INT_MIN specially
                    if (promoted_r == (std::numeric_limits<intmax_t>::min)())
                    {
                        constexpr uintmax_t abs_min =
                            static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                        
                        auto prod = promoted_l * abs_min;
                        
                        // Check overflow
                        if (prod / promoted_l != abs_min || prod / abs_min != promoted_l)
                        {
                            throw std::overflow_error("integer overflow");
                        }
                        
                        if (prod > abs_min)
                        {
                            throw std::overflow_error("integer overflow");
                        }
                        
                        if (prod == abs_min)
                        {
                            return (std::numeric_limits<ResultT>::min)();
                        }
                        
                        return m::try_cast<ResultT>(-static_cast<intmax_t>(prod));
                    }
                    
                    auto abs_r = static_cast<uintmax_t>(-promoted_r);
                    auto prod = promoted_l * abs_r;
                    
                    // Check overflow
                    if (prod / promoted_l != abs_r || prod / abs_r != promoted_l)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    // Negate and check it fits
                    constexpr uintmax_t max_negative =
                        static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                    
                    if (prod > max_negative)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    if (prod == max_negative)
                    {
                        return m::try_cast<ResultT>((std::numeric_limits<intmax_t>::min)());
                    }
                    
                    return m::try_cast<ResultT>(-static_cast<intmax_t>(prod));
                }
            }

            '''
    content = content[:pos4] + multiply_code + content[pos4:]
    print("Added multiply to unsigned×signed→signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 5. Add multiply to signed + unsigned -> unsigned specialization
marker5 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Signed / unsigned with unsigned result.
                // Result must be non-negative, so l must be non-negative.
                
                if (r == 0)
                    throw std::overflow_error("integer overflow");
            
                if (l < 0)'''

pos5 = content.find(marker5)
if pos5 != -1:
    multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Signed × unsigned with unsigned result.
                // Result must be non-negative, so l must be non-negative.
                
                if (l == 0 || r == 0)
                    return 0;
                
                if (l < 0)
                {
                    // Negative × positive = negative (can't represent in unsigned)
                    throw std::overflow_error("integer overflow");
                }
                
                // Both effectively unsigned now
                auto l_as_unsigned = static_cast<uintmax_t>(l);
                auto r_promoted = static_cast<uintmax_t>(r);
                
                auto prod = l_as_unsigned * r_promoted;
                
                // Check overflow
                if (prod / l_as_unsigned != r_promoted || prod / r_promoted != l_as_unsigned)
                {
                    throw std::overflow_error("integer overflow");
                }
                
                return m::try_cast<ResultT>(prod);
            }

            '''
    content = content[:pos5] + multiply_code + content[pos5:]
    print("Added multiply to signed×unsigned→unsigned")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# 6. Add multiply to signed + signed -> signed specialization
marker6 = '''            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Signed / signed with signed result.
                // Special case: INT_MIN / -1 = overflow (result would be INT_MAX + 1)
                
                if (r == 0)
                    throw std::overflow_error("integer overflow");
            
                auto promoted_l = static_cast<intmax_t>(l);
                auto promoted_r = static_cast<intmax_t>(r);'''

pos6 = content.find(marker6)
if pos6 != -1:
    multiply_code = '''            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Signed × signed with signed result.
                
                if (l == 0 || r == 0)
                    return 0;
                
                auto promoted_l = static_cast<intmax_t>(l);
                auto promoted_r = static_cast<intmax_t>(r);
                
                // Determine the sign of the result
                bool result_negative = (promoted_l < 0) != (promoted_r < 0);
                
                // Work with absolute values
                uintmax_t abs_l, abs_r;
                
                if (promoted_l == (std::numeric_limits<intmax_t>::min)())
                {
                    abs_l = static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                }
                else
                {
                    abs_l = static_cast<uintmax_t>(promoted_l < 0 ? -promoted_l : promoted_l);
                }
                
                if (promoted_r == (std::numeric_limits<intmax_t>::min)())
                {
                    abs_r = static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                }
                else
                {
                    abs_r = static_cast<uintmax_t>(promoted_r < 0 ? -promoted_r : promoted_r);
                }
                
                auto prod = abs_l * abs_r;
                
                // Check for overflow in the multiplication itself
                if (prod / abs_l != abs_r || prod / abs_r != abs_l)
                {
                    throw std::overflow_error("integer overflow");
                }
                
                // Check if the result fits in the signed range
                constexpr uintmax_t max_positive = static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)());
                constexpr uintmax_t max_negative = max_positive + 1;
                
                if (result_negative)
                {
                    if (prod > max_negative)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    if (prod == max_negative)
                    {
                        return m::try_cast<ResultT>((std::numeric_limits<intmax_t>::min)());
                    }
                    
                    return m::try_cast<ResultT>(-static_cast<intmax_t>(prod));
                }
                else
                {
                    if (prod > max_positive)
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    
                    return m::try_cast<ResultT>(static_cast<intmax_t>(prod));
                }
            }

            '''
    content = content[:pos6] + multiply_code + content[pos6:]
    print("Added multiply to signed×signed→signed")

# Final save
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

print("\nMultiplication implementation complete!")
