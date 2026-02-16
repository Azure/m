---
applyTo: "**/*.h"
applyTo: "**/*.hxx"
applyTo: "**/*.hh"
applyTo: "**/*.c"
applyTo: "**/*.cpp"
applyTo: "**/*.cxx"
applyTo: "**/*.cc"
---

# Rules for C and C++

Write code that is memory safe.

Add comments that are informative but not redundant. If the intent is clear from
the code there is no need to comment but do comment design intent or decisions
that led to aspects of the implementation that would not be considered normal
or obvious.

# Language standard

In general use C++20 as the C++ language standard.

In some cases, C++23 is required because of new constructs in the language or
the standard library but only use these when they provide exceptional unique
value.

## Casting

Avoid C-style casts whenever possible. Prefer a possibly lengthy series of casts through
const_cast<>(), static_cast<>() and reinterpret_cast<>() over a C-style cast since the
explicit casting makes the series of type transformations explicit, even if wordy.

The `m` library provides the `m::to<T>()` cast which is an arithmatically "safe" cast
rather than static_cast<>() which will truncate values without warning. When casting
from a larger type to a smaller type and the value has not been proven to be safely
within bounds for the smaller type, either use `m::to<T>()` or comment why it is not
being used.

# Target compilers

The target compilers are largely a function of the language standard. Ideally `m` would
build with any compiler that had a fairly broad set of C++20 support.

Unfortunately as of at least late 2025, G++ does not have sufficient C++20 support
for what we need and had to be removed from the multiplex used in the CI for
cross platform validation.

As such, MSVC and Clang are the compilers used.

GCC is welcome once it supports our features, or if someone were to take the time to
specialize the code for it, but at the same time, we don't want to become an overly
bifurcated code base to enable older compilers.

We would *like* to require C++23 as soon as possible, not make the source code
more complex to enable compilers that can't have a broad set of C++20 features
enabled. No disrespect to G++ intended, this is probably a library issue, the CI
builds stopped working, I found the problem to be a G++ problem, turned of G++
and haven't looked back.

# Build System

M builds using CMake which has some ups and downs.

## Libraries

CMake libraries appear to require at least one compiland. This is problematic for
header-only libraries and forces them to have at least an empty .cpp file. If there
is a simple solution to this, please go ahead and fix the CMakelists.txt files
and remove this item from the instructions.

Libraries should, in general, depend on the minimum set of other libraries that
they can. This is general dependency management goodness but perhaps needs to be said.

# Modules (the actual C++ feature, not the general concept)

The import modules feature of C++ seems attractive but it's unclear how it could
be useful or used in `m`.

As a result we are not using it.

# Testing

All code should have unit tests for it. This is not always possible, but whenever
possible, both positive and negative cases should be covered by unit tests.

Use the "GoogleTest" / "gtest" framework to do the testing.

When an exception is known to be thrown, use the gtest macros to expect the
specific exception, with the narrowest possible scope.

## Mocking, faking, etc

There is no global recommendation on this topic at this time. Use situationally
appropriate decisions.


