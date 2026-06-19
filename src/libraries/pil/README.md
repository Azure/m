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

## Hostable Web Core (HWC) — optional, never required to build or to run the default tests

PIL has a planned third isolation surface that wraps **IIS Hostable Web Core**
(`hwebcore.dll`) so IIS-hosted scenarios can be recorded, replayed, and fault-injected.
See `DESIGN-NOTES.md` decisions **D-HWC-1 … D-HWC-7** and the `M-HWC-*` milestones in
`CHECKLIST.md`.

### Hard rule: HWC is an optional runtime dependency

- **Building PIL never requires HWC.** The engine is bound at runtime with
  `LoadLibraryExW` from the absolute `%windir%\system32\inetsrv\hwebcore.dll` path (with
  `inetsrv` added to the dependency search, since the engine's sibling DLLs live there) +
  `GetProcAddress` (decision D-HWC-3) — there is no import library, no
  `__declspec(dllimport)`, and no link-time edge on anything IIS. A machine without the
  HWC feature compiles PIL (and `mwin32`) exactly the same as one with it.
- **The default regression tests never require HWC.** The direct webcore provider takes
  its three engine entry points (`WebCoreActivate` / `WebCoreShutdown` /
  `WebCoreSetMetadata`) through an injectable function-pointer seam. Default unit/CTest
  runs supply a **fake engine** (a different function-pointer triple) and exercise the
  full activate / shutdown / set_metadata lifecycle without `hwebcore.dll` present.
- **Only opt-in integration tests and certain tools use the real engine.** Any test that
  drives the genuine `hwebcore.dll` must detect HWC at runtime and **skip** (not fail)
  when it is absent, and must be gated out of the default CTest set (e.g. a dedicated
  label / suite that the default `ctest` invocation does not select). Tools that host a
  live web core obviously require the feature to *run*, but must still *build* without it.

If you add HWC-backed code, preserve all three guarantees above. A green build and a
green default `ctest` on a machine with no IIS feature installed is part of the
definition of done.

### Installing HWC (only if you want to run the real-engine tests/tools)

The feature ships `hwebcore.dll` to `%windir%\system32\inetsrv` plus its IIS
dependencies. Install it from an **elevated** shell:

```powershell
# either of these (both need admin):
dism /Online /Enable-Feature /FeatureName:IIS-HostableWebCore /All /NoRestart
# or
Enable-WindowsOptionalFeature -Online -FeatureName IIS-HostableWebCore -All -NoRestart
```

Verify it is present (note the `inetsrv` subfolder — the DLL is **not** in `system32`
directly, and depends on sibling DLLs in `inetsrv`, so a bare-name load fails with
`ERROR_MOD_NOT_FOUND`):

```powershell
Test-Path "$env:windir\system32\inetsrv\hwebcore.dll"   # -> True once installed
```

The Windows SDK header `um/hwebcore.h` (the entry-point prototypes) is part of the SDK
and is **not** the same as the feature — you do not need the feature installed to build
against the header (we resolve the entries dynamically, so we do not even link the
header's declarations into an import).

The network edge (`http.sys` URL reservations, HTTPS cert bindings) is handled by the
deferred `ihttp_listener` namespace-redirection surface (D-HWC-6); the in-process fake
edge (Tier B) needs no admin, no URL ACL, and no real `http.sys`.

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

