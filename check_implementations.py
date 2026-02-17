#!/usr/bin/env python3
import sys

try:
    with open('src/libraries/math/include/m/math/math.h', 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()
except:
    with open('src/libraries/math/include/m/math/math.h', 'r', encoding='latin-1') as f:
        lines = f.readlines()

# Find lines that contain "divide(LeftT l, RightT r)"
divide_lines = []
for i, line in enumerate(lines):
    if 'divide(LeftT l, RightT r)' in line:
        divide_lines.append((i, line))

print(f"Found {len(divide_lines)} divide implementations at lines:")
for line_num, line in divide_lines:
    print(f"  Line {line_num + 1}: {line.strip()[:60]}")

# Find lines that contain "multiply(LeftT l, RightT r)"
multiply_lines = []
for i, line in enumerate(lines):
    if 'multiply(LeftT l, RightT r)' in line:
        multiply_lines.append((i, line))

print(f"\nFound {len(multiply_lines)} multiply implementations at lines:")
for line_num, line in multiply_lines:
    print(f"  Line {line_num + 1}: {line.strip()[:60]}")
