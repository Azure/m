# What is a "chonk"

The chonk library is equivalent to what I later discovered already had a name:

`std::inline_vector`

It turns out that the proposal to the standards committee even had a
proposed implementation that was published on Godbolt. I picked it up,
fixed a few things that were alien to MSVC, changed the names to not
be invalid for a library to provide (leading underscores, trailing
underscores, invading the `std::` namespace) and put it in the
m_inline_vector library.

The m_chonk unit tests even passed with it on the first try, although
they were pretty minimal at that point.

Chonk has an additional feature that the std::inline_vector<> lacks
which is that the insert() member function for m::chonk<> has a
callout for the overflow element. This is designed to make implementation
of a chonk that is part of a list of chonks or vector of chonks
easier.

For this reason, the chonk library is moved under the `archive` section.
