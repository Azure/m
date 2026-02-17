---
applyTo: "src/library/math/**/*"
---
# Copilot special instructions for math

In general, the conceptual model for math we take in the math library is that the computation is
performed abstractly without bounds, and then the result is attempted to be cast to the result type
specified.

Sometimes this makes the computation more complex than the typical "checked math" implementation,
but for most type combinations, they are normal operations.
