# Guidelines on m::atomic

It's easy to make statements about how std::atomic is lacking.

The difficulty is making proofs around how improvements are correct.

Be _extremely_ judicious in additions here to things that can almost certainly be
seen as correct without resorting to massive proofs or lengthy verification
runs.

For example, with a coworker, I often have debates about the degree to which
memory_order_acquire and memory_order_release have to be paired on particular
memory objects vs. not and his opinions are driven based on common hardware
implementations. I _believe_ that WG21's memory model document is clear that
the orderings must be paired on specific objects to form synchronization.
(This is regarding operations on "atomic" objects, not the fence / barrier
operations which seem to be designed to evoke the hardware concepts, and
where I have a harder time understanding the software level equivalencies.)

There are some patterns which have been shown to be safe and some which have
been shown to not be safe even though many people believe them to be. I will
quickly try to explain some of each:

## Safe Patterns

If you do not understand how the compiler and the execution machine may reorder
memory accesses, you should learn about that now. It's well beyond the scope
of this document to try to teach it.

### Safe Pattern 1: Use `std::memory_order_seq_cst`

This is generally the "safe default" for atomic access. It's also generally
recognized as the slowest and most demanding on the memory subsystem but it's
better than having errors.

Some people imagine that sequential consistency is equivalent to "both acquire
and release at the same time" but this isn't quite correct and the differences
are subtle. On machines which support it, is usually maps to "Total Store
Order", but this depends on your compiler and standard library implementation,
don't take it from me here. The C++ standard makes no such promises.

It _is_ the most conservative memory ordering and if you know you cannot use
a std::mutex for some reason but must manage cross-thread sharing of data
without having greater understanding of the more esoteric memory models,
this is the one to choose.

### Safe Pattern 2: Use `std::memory_order_acquire`, `std::memory_order_release`

[There's an addendum to use `std::memory_order_acq_rel` for read-modify-write
operations but the heading was already too long.]

If you understand what "acquire" and "release" are, and you are not tempted to
mix their use with other memory orders, go ahead and use them. They appear to
work correctly on all platforms.

Their performance is not what it should be before some ARM architectural
version (v8.2 with some addendum?) on aarch64/arm64 but the functionality
is good and can mostly be described as "load with `std::memory_order_acquire`,
store with `std::memory_order_release`, and exchange with
`std::memory_order_acq_rel`."

Again we're not going to try to teach what acquire and release are here and
when it's appropriate to use them but using them uniformly is a good pattern.

It's _extremely tempting_ to try to escape the pattern and use
`std::memory_order_relaxed` for loads in some patterns.

There are some good arguments for this when scoped to hardware with certain
characteristics. I do not believe the C++ standard, by itself, guarantees
those characteristics, so please do not check such code in. At least not
to the non-platform-specific portion of the repository.

## Dangerous Patterns

### Dangerous Pattern 1: Mixing `std::memory_order_seq_cst` and others

This seems like it would be innocuous since this is more or less how
all Windows code was written up to a point - use the
`::InterlockedCompareExchange()` functions (which are _perhaps_
arguably implicitly using seq_cst memory order) _sometimes_ and
regular memory accesses elsewhere.

First, the standard says nothing about how synchronization between
storage using different memory orders is ordered.

The other thing is that this has been shown to cause memory writes to be
lost because in practice the directives to the CPU's memory buffers
are different for, for example, TSO and Release writes. (I believe
the root cause here being that Release writes end up tagging the
cache lines with a new dirty cache line identity while the TSO uses
a different mechanism so when the Acquire comes along, and Acquires
usually pair with Releases to drive cache synchronization, since there
was no Release store, the cache line that was dirtied by the TSO write
can be missed. But since no vendor actually publicly shares their
internal cache coherency protocols, everyone guesses based on the
seminal papers from the 1960s and 1970s.)

