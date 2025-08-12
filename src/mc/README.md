# What is "mc"?

The majority of the "m" work has been to try to innovate in the C++
world, to deliver what, to the author(s) has seemed like obvious
missing features for modern software development in a way that
is rigorous (hopefully!) and meshes with the common in-use platforms:
Microsoft Windows and Linux.

However, one of the authors finds the tenets of the Standard Library's
collection types extremely awkward to work with and harkens back to
the 1990s and 2000s, a time before the STL took over the space, and
while the world was much rougher and less well thought out in general,
some of the collection types had better interfaces.

mc is a set of interfaces to collections that attempt to provide more
sane interfaces to working with collections than what the standard
library provides.

What, exactly, does this mean? At the time of this writing (August 12, 2025),
that is still a work in progress to get overly precise. However the
tenets are:

- Dealing with iterators is a pain
- You shouldn't have to interact with a collection multiple times to perform
what to "most people" seem like atomic operations
- Perhaps we should employ some smart notions like invocables/callables and
std::optional.

If std::variant were as usable as, say, Rust enums, there might be even more
fun to be had, but even after assiduously using them as much as possible,
they are, to say the least, a bear to work with. [Ed. note: I, personally, have
not considered whether there is a less space efficient but easier to use
alternative to the std::variant implementation which might be a good
choice to consider. The std::visit vs. switch or match coding pattern is
really not a reasonable comparison unless one were to try to use
Macro Magic to hide it and a C++ feature in this day and age that demands
use of macros to make it usable... is not a feature.]

# Are the Standard Library collections ... OK?

As data structures, one can have comments about how the standard library
implemented Red-Black trees or hash tables, but on the whole they are fine.
There is no "need" to reimplement them, except for the sake of efficiency.

A canonical example might be this. Working with a queue, ignoring the
member functions on std::queue<>, you might say, well, I want to add an
element to the end of the queue, and I want to try to remove the head
element.

BUT - the STL designers say - copying out the head element may not be
possible because the queue could be empty, AND copying the element could
be expensive to copy! So instead, we shall give you a way to test if
the queue is empty and a way to get a reference to the head element!

Now, your code which conceptually should look something like:

```
    auto const e = q.pop();
    if (e) {
        // Hey we got one!
    }
```

instead looks like:

```
    if (!q.empty()) {
        auto e = q.front();

        // work with e

        q.pop_front(); // don't forget to actually get rid of it after you're done with it
    }
```

or, I have a notion of a "_with()" idiom for applying "the thing" to a provided lambda (needs
wordsmithing, the _with() naming works better with locks):

```
    q.pop_with([&](auto&& e) { 
        // work with e
    );
```

In this case, `pop_with()` would return a `bool` indicating whether a
pop / invocation had occurred. Perhaps no suffix is needed here? "into"?

Like I said, wordsmithing.

## Thick or thin?

This can be accomplished as a thin veneer or something deeper?

The principle of "leaky abstractions" says that a thin veneer is unlikely
to hold as being thin but perhaps will be adequate if the existing collection
is always contained within the `mc` container, rather than used as a
base class. The STL containers are not particularly well suited to be
base classes in any case unless specifically documented as such.

On the other hand, the convolutions that may be needed to expose the
"rational" interface on top of the STL interface may be extreme
and at some point warrant a new collection implementation. This should
be driven by data based on inspection of the layout of actual
data structures and generated code.

One of the personal motivations for considering this is that the earlier
example of inspecting the queue for being empty, accessing the front
member, and then having to remove the member from the queue at the
end of the operation in practice ends up being a lot of separate loads
of the pointers to the queue in memory even in optimized builds, at
least with up to date MSVC on x64. I should compare with clang and gcc
also at -o2 optimization levels.

Modern processors are very good at dealing with this kind of terrible
code but still the instruction density if nothing else.

We will start with the thin veneers for some of the obvious cases in order
to prove out the interfaces to begin with. `std::queue<>` and
`std::deque<>` should be simple and establish some good patterns.

`std::map<>` is the really interesting one. Nobody really, in this author's
opinion, wants to deal with `std::pair<>` or iterators when performing
CRUD operations on a tree.

Everything _must_ still, at the end of the day, work with `for (auto&& e : coll)`.
So iterators will be yielded, they just should not be the cornerstone of the
"day to day" interaction with the CRUD-type operations on a collection.

This _should_ also enable algorithms and ranges although ranges are
so hopelessly complex that all I can hope for is to supply
`.begin()` and `.end()` and pray.

## Does that mean everything?

Absolutely not!

There has been a great deal of solid work in the language and the library
that shows no real obfuscation, thank goodness.

The more modern iterator library, concepts, chrono, type_traits to name
a few.

It's really the O.G. collections that need re-working.

