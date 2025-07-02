# The Platform Isolation Library

A library that gives an abstration ostensibly over "any" platform but
at least initially targeted over the general data stores provided by
the Microsoft Windows platform.

There are several core aspects to the PIL:

* An easy-to-use C++ binding to the platform APIs that gives much
more straightforward modern code for the C++23 and later client.

* A set of composible layers that can be used to redirect path based
access to system resources for testing purposes to other paths, or
to buffer them strictly in memory, logging the results either as
deltas or the final state.

* A set of virtual interfaces that are used to compose these capabilities
that can be extended for additional functionality that the authors
did not initially imagine.

## Using the PIL

## Future Work

### Iteration (non-vtable)

Currently the easy to use interface returns `std::vector<T>` which isn't terrible
to consume but at the same time means that if it's a large collection:
* there's
a big contiguous array
* it could be a long delay before the
first item is returned
* If you only wanted the first 'n' elements, you can't stop there

It would be much better to return a forward-only const iterator but that means
creating a type that mocks an iterator over the vtable and also the sentinel for
`std::end(collection)`.

Not rocket science, but an hour or two of coding the faux iterator and sentinel
and then writing tests. It just wasn't done in the initial push.

### Iteration (vtable)

It would be nice if the virtual interface grabbed more than one item at a time.

The initial registry enumeration virtual interface directly reflects the
Win32 API which is not the end of the world but also kind of gross. At the
very least, an API that looked more like:

```
virtual disposition_type
enum(
    flags_type flags,
    size_t starting_index,
    std::span<whatever_value_type, std::dynamic>& values) = 0
```

would be much better since you could pass a stack buffer of `values.size()` `whatever_value_type`s to fetch.
(note pattern of passing a reference to a dynamic span so the span gets overwritten
on exit with the correct size.)

