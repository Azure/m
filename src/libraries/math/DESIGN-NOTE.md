# m::math Design Note

## 1. Foundational Axiom

Every operation is performed in Z (unbounded integers) then cast to ResultT.

The only rule: if result_in_Z is not in [ResultT::min, ResultT::max], throw overflow_error.

Signedness of operands does not matter; only the result value matters.
