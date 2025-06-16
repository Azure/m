# What is in Windows for m and What is Not

The `m` library is intended to be a modern C++ library for platform independent code.

In some cases, platforms provide *significant* improvements over what the C++ standard
only can provide. (Examples notably include specific details around interactions with
the filesystem and the capabilities of the Windows threadpool.) In those cases,
Windows specific code is provided, usually to enable the generic capability with
enhanced performance or function on Windows.

Hopefully Linux (and other platform!) users will provide similar capabilities in the
Linux space.

When working with an operating system platform like Windows, there are some "table
stakes" to consider especially in a "modern C++" environment, like RAII types for
managing operating system objects.

Windows has done a good job of finally promoting a set of types in the "Windows
Implementation Library", or `wil` for managing these sorts of C++-isms. There are
two fundamental problems with it.

First, while `wil` does a very good job at managing the Windows resources that
it manages and also codifying a number of somewhat entrenched 1990s / 2000s era
C/C++ programming practices, it does not do that great of a job of exposing a
modern C++ programming interface. That's OK though, maybe it is just scaffolding
for building your own modern C++ interface; you just cannot rely on `wil` to be
any of the types or concepts that you expose yourself.

The second problem is a harder one for the `m` project. `wil` does not make itself
readily available for CMake based consumption. It can be configured into your larger
project into your `vcpkg` pre-build configuration requirements. Probably almost
everyone uses `vcpkg` at this point. At least the `vcpkg` team tells me this is
true.

However, both `wil` and `gsl` do not provide easy consumption paths for CMake
based builds, other than "get them configured via vcpkg and then run the CMake build."

[Slipping into first person for a moment...]

I might be convinced that this is a reasonable tact to take except for the fact that
I tried all the suggestions that people had for me and what things I could find via
the usual Google / Bing / ChatGPT / GitHub search to make the use of vcpkg to pick
up both `wil` and `gsl` for `m`, and none of them could be configured reliably
on:
1. My developer workstation
1. The CI system on GitHub
1. My developer workstation for the downstream dependent of the vcpkg produced by `m`
1. The CI system for the downstream dependent

So all in all, it just didn't work and I ended up backing out all `wil` and `gsl`
usage which was time consuming, annoying, and frustrating.

[end of first person aside]

## What's not in `m` / `Windows`

The `m` project has no plans to provide some general purpose Windows "wrappers".

Because of the aforementioned problems using the generally available `wil` project,
it does have some hopefully well designed types for managing common Windows
patterns which can use adaptation to C++ modern design patterns, and depending
on their level of maturity, these may or may not be in "details" or "impl"
namespaces or classes.

For example, as part of developing the filesystem change notification monitor, a
class was authored to manage the lifetime of a `HANDLE` and call `CloseHandle()`
on it when it goes out of scope. The design of this class matches the general
patterns established by the C++ standard for this kind of resource management
and will be useful for other classes in the `m`/`Windows` area, so it will be
made public. That does not mean that it is the beginning of an entire suite
of classes to cover every aspect of the Windows API set, like `wil` aspires
to.

In fact, the entire area of "direct contact with the Windows API" should be
considered experimental.

There are two general philosophies to consider in the design space here:

1. The Windows API is the canonical reference. Any deviation from it is incorrect
by definition and prone to growing deviatioon over time.
1. Abstractions should be formed on their own, whole, which have merit and while they
may be influenced by a platform, their design should be rigorous and not be subject to
the whims of the bugs and evolution of the platform.

The filesystem monitor attempts to hold this second stance. While the primary client for
the API is on Windows and the ReadDirectoryChanges API is clearly
directory-focused, the API is resolutely clear that it is about registering
for changes on
a single file within a directory. There are open design issues on how transparent the API
has to be with respect to the operating system's / filesystem's change notifications
vs. actual changes to files.

The `wil` library clearly takes the first stance, because its primary job is to 
give safety to the Windows API. The fact that there are some number of helper
functions that make life better using "STL" strings is nice for the modern C++
developer but is, relatively speaking, the minority of the `wil` code.

`m` is not `wil` and cannot and will not go down some path of supporting the
Windows API / SDK.

It is exposing a better filesystem API, augmenting what std::filesystem provides.

It also provides a better set of fluent string conversions with the m::to_[*]string()
functions.


