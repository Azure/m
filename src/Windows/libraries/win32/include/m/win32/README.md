# Why does m have a `win32` library?

Really, probably, it shouldn't.

Speaking for myself, my overall design goal for `m` has always been to provide features primarily targeting a specific set
of applications really for myself but ideally as much as possible in a platform-independent fashion, with the minimum
set of platform-specific support code written. At some point you need to call open() or CreateFileW() in your call
stack, so _something_ has to adapt to the platforms.

For the most part, `m` is platform independent and attempts to be compiler neutral also. The fact that gcc is not part
of the default set of compilers that it tests with is more of a statement about gcc's support of the language
standards and versions than some statement about gcc. If gcc had a correct, _alternative_ interpretation of the
standard, `m` would strive to conform to it also.

However my primary consumer also needs some code to run very well on Windows and that needs some standard Windows
specific resource management types to manage them.

Microsoft provides a package called `wsl` that does this, but I have had a hard time integrating it into my
builds. Truthfully, that was kind of in the distant past and I may know how to do it now that I've leveled
up my vcpkg and CMake skills quite a bit but at this time I'm goint to write this readme rather than try to
migrate over to the `wsl`-equivalent types, since `wsl` has unknown encumberances in terms of possible
DLL dependencies, downstream dependencices, etc. Minimizing dependency management has been the hallmark of
my career and I don't see any reason to stop now.

If in the future we want to switch over, I don't have any particular reason not to, other than the burden that
this places on clients of `m`. If there is zero additional burden (e.g. `m` is static library only, don't
force some DLL installation into the mix, don't bring some heavyweight package that may require more complex
configuration in tandem with the existing network of dependencies that the project has) and/or _it is worth
the enlarged dependency footprint_, go for it.

(So, while I say go for it, I would really say don't do it without just cause.)

# What's in the `win32` library

Keep the contents of what's here minimal. For each header, have a type which represents a type of 'object'
generally documented in the Win32 documentation on Microsoft Learn.

## Windows kernel objects

The object should not be copyable, but should be movable. Don't try to do something clever like duplicating the handle
for copy. Nobody really expects to deal with duplicated handles, the ramifications of duplicated handles and the
relationship between handles and access control are generally not understood.

## Non-kernel objects

Use your best judgement for modern C++. If the type is a "value type" in that it "feels" like a value then you
should implement all the operations so that it can be copied etc. Otherwise, be sure to delete the copy
operations using `= delete` to ensure that the compiler does not generate them for you and so that readers
will see the explicit deletion to remove ambiguity.

Otherwise, when in doubt, make the object movable but not copyable. This is a reasonable working model for
many objects, and most modern C++ containers can work reasonably with objects that are movable but not
copyable.
