#!/usr/bin/env python3
# Script to add divide() implementations to all safe_math_helper specializations

import re

# Read the file
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Find and add divide to unsigned + unsigned -> signed specialization
marker1 = '''        //
        // Handle (unsigned [op] unsigned) -> signed
        //'''

# Find the position and the struct
pos1 = content.find(marker1)
if pos1 != -1:
    # Find the closing of multiply method and add divide before the closing brace of struct
    # Look for the pattern after marker1
    search_start = pos1 + len(marker1)
    # Find the struct closing (look for the pattern where subtract ends)
    pattern = r'(return m::try_cast<ResultT>\(Doppelganger::subtract\(l, r\)\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+5000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Division of two unsigned values with signed result.
            // Use the unsigned/unsigned divide and cast to signed.
            return m::try_cast<ResultT>(Doppelganger::divide(l, r));
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to unsigned/unsigned->signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Add divide to unsigned + signed -> unsigned specialization
marker2 = '''        //
        // Handle (unsigned [op] signed) -> unsigned
        //'''

pos2 = content.find(marker2)
if pos2 != -1:
    search_start = pos2 + len(marker2)
    # Find the subtract method's closing brace within this specialization
    pattern = r'(return m::try_cast<ResultT>\(static_cast<uintmax_t>\(l\) - static_cast<uintmax_t>\(r\)\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+6000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Unsigned / signed with unsigned result.
            // If r is negative, the mathematical result is negative (can't fit in unsigned).
            // If r is positive, perform unsigned/unsigned division.
            
            if (r == 0)
                throw std::overflow_error("integer overflow");
                
            if (r < 0)
            {
                // Dividing positive by negative gives negative result
                throw std::overflow_error("integer overflow");
            }
            
            // r is positive, safe to cast to unsigned
            auto l_promoted = static_cast<uintmax_t>(l);
            auto r_as_unsigned = static_cast<uintmax_t>(r);
            
            auto quot = l_promoted / r_as_unsigned;
            
            return m::try_cast<ResultT>(quot);
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to unsigned/signed->unsigned")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Add divide to signed + unsigned -> signed specialization  
marker3 = '''        //
        // Handle (signed [op] unsigned) -> signed
        //'''

pos3 = content.find(marker3)
if pos3 != -1:
    search_start = pos3 + len(marker3)
    # Find the subtract method's end
    pattern = r'(return m::try_cast<ResultT>\(promoted_r\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+8000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Signed / unsigned with signed result.
            
            if (r == 0)
                throw std::overflow_error("integer overflow");
            
            auto promoted_l = static_cast<intmax_t>(l);
            auto promoted_r = static_cast<uintmax_t>(r);
            
            // For signed/unsigned division:
            // - If l is negative, result is negative or zero
            // - If l is positive, result is positive or zero
            // The result magnitude is |l|/r which is always <= |l|
            
            if (promoted_l >= 0)
            {
                // Positive / unsigned: treat as unsigned division
                auto l_as_unsigned = static_cast<uintmax_t>(promoted_l);
                auto quot = l_as_unsigned / promoted_r;
                return m::try_cast<ResultT>(quot);
            }
            else
            {
                // Negative / unsigned: result is negative
                // Compute |l| / r, then negate
                
                // Handle INT_MIN specially
                if (promoted_l == (std::numeric_limits<intmax_t>::min)())
                {
                    // |INT_MIN| is INT_MAX + 1
                    constexpr uintmax_t abs_min =
                        static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
                    auto quot = abs_min / promoted_r;
                    // Negate the result
                    if (quot > static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                    {
                        // Result would be < INT_MIN
                        throw std::overflow_error("integer overflow");
                    }
                    return m::try_cast<ResultT>(-static_cast<intmax_t>(quot));
                }
                
                auto abs_l = static_cast<uintmax_t>(-promoted_l);
                auto quot = abs_l / promoted_r;
                
                // Negate and check it fits
                if (quot > static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                {
                    throw std::overflow_error("integer overflow");
                }
                
                return m::try_cast<ResultT>(-static_cast<intmax_t>(quot));
            }
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to signed/unsigned->signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Add divide to unsigned + signed -> signed specialization
marker4 = '''        //
        // Handle (unsigned [op] signed) -> signed
        //'''

pos4 = content.find(marker4)
if pos4 != -1:
    search_start = pos4 + len(marker4)
    # Find subtract's end
    pattern = r'(return add\(l, r_as_unsigned\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+8000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Unsigned / signed with signed result.
            
            if (r == 0)
                throw std::overflow_error("integer overflow");
            
            if (r < 0)
            {
                // Unsigned / negative = negative or zero
                // Result is -(l / |r|)
                
                if (r == (std::numeric_limits<RightT>::min)())
                {
                    // Handle most negative value specially
                    constexpr uintmax_t abs_min =
                        static_cast<uintmax_t>(-(static_cast<intmax_t>(
                            (std::numeric_limits<RightT>::min)()) + 1)) + 1;
                    auto l_promoted = static_cast<uintmax_t>(l);
                    auto quot = l_promoted / abs_min;
                    
                    if (quot > static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    return m::try_cast<ResultT>(-static_cast<intmax_t>(quot));
                }
                
                auto l_promoted = static_cast<uintmax_t>(l);
                auto abs_r = static_cast<uintmax_t>(-static_cast<intmax_t>(r));
                auto quot = l_promoted / abs_r;
                
                if (quot > static_cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                {
                    throw std::overflow_error("integer overflow");
                }
                return m::try_cast<ResultT>(-static_cast<intmax_t>(quot));
            }
            else
            {
                // Unsigned / positive signed = positive
                auto l_promoted = static_cast<uintmax_t>(l);
                auto r_as_unsigned = static_cast<uintmax_t>(r);
                auto quot = l_promoted / r_as_unsigned;
                return m::try_cast<ResultT>(quot);
            }
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to unsigned/signed->signed")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Add divide to signed + unsigned -> unsigned specialization
marker5 = '''        //
        // Handle (signed [op] unsigned) -> unsigned
        //'''

pos5 = content.find(marker5)
if pos5 != -1:
    search_start = pos5 + len(marker5)
    # Find subtract's end in this specialization
    pattern = r'(return m::try_cast<ResultT>\(diff\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+5000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Signed / unsigned with unsigned result.
            // Result must be non-negative, so l must be non-negative.
            
            if (r == 0)
                throw std::overflow_error("integer overflow");
            
            if (l < 0)
            {
                // Negative / positive = negative (can't represent in unsigned)
                throw std::overflow_error("integer overflow");
            }
            
            // Both effectively unsigned now
            auto l_as_unsigned = static_cast<uintmax_t>(l);
            auto r_promoted = static_cast<uintmax_t>(r);
            
            auto quot = l_as_unsigned / r_promoted;
            
            return m::try_cast<ResultT>(quot);
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to signed/unsigned->unsigned")

# Save intermediate
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

# Reload
with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8') as f:
    content = f.read()

# Add divide to signed + signed -> unsigned specialization  
marker6 = '''        //
        // Handle (signed [op] signed) -> unsigned
        //'''

# This one is tricky - need to find the right specialization
# Let me search more carefully
# Actually, I realize I haven't seen this specialization yet in the fixes
# Let me check if it exists and has the right pattern

# For now, let's handle signed + signed -> signed which we know exists
marker7 = '''        //
        // Handle (signed [op] signed) -> signed
        //'''

pos7 = content.find(marker7)
if pos7 != -1:
    search_start = pos7 + len(marker7)
    # Find the subtract method's end
    pattern = r'(return m::try_cast<ResultT>\(rv\);\s+}\s+)(};)'
    match = re.search(pattern, content[search_start:search_start+6000])
    if match:
        insert_pos = search_start + match.start(2)
        divide_code = '''
        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Signed / signed with signed result.
            // Special case: INT_MIN / -1 = overflow (result would be INT_MAX + 1)
            
            if (r == 0)
                throw std::overflow_error("integer overflow");
            
            auto promoted_l = static_cast<intmax_t>(l);
            auto promoted_r = static_cast<intmax_t>(r);
            
            // Check for INT_MIN / -1
            if (promoted_l == (std::numeric_limits<intmax_t>::min)() && promoted_r == -1)
            {
                throw std::overflow_error("integer overflow");
            }
            
            auto quot = promoted_l / promoted_r;
            
            return m::try_cast<ResultT>(quot);
        }
        '''
        content = content[:insert_pos] + divide_code + '\n        ' + content[insert_pos:]
        print("Added divide to signed/signed->signed")

# Final save
with open('src/libraries/math/include/m/math/math.h', 'w', encoding='utf-8') as f:
    f.write(content)

print("\nDivision implementation complete!")
print("Note: signed/signed->unsigned specialization may not exist and needs separate handling")
