---
applyTo: "**/*"
---

# Layering rules

It is easy to end up with circularity of dependencies so we have to have some amount of formal
rules of layering in the library.

We will try to enumerate these and refine them over time here.

Sometimes the entities listed here are actual CMake libraries but since in C++ the coin of the
realm are headers, sometimes they are header files. We will list the headers by the source
path.

CoPilot should update the header paths here in this document when they change in the repository
and so should humans if they move the headers.

Components at a given layer may refer to libraries at the same layer or lower but not to components
at a higher layer.

# Layer 0: Foundation

The bottom most layer of the repository consists of the following libraries:

- The contents of the `src/include` directory (repository headers)
- The `src/libraries/cast` library
- The `src/libraries/math` library

Note: Paths are relative to the repository root. Use forward slashes for cross-platform compatibility.



# Layer Infinity: Everything else

The remainder of code in the repository that has not been characterized is not in any particular layer and
can be considered to be in a layer above any other. Colloquially, you could call this "the infinity layer"
but any good mathematician would tell you there is no number called infinity.

