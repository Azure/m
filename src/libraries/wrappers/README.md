# What is m/src/Windows/libraries/wrappers?

The Win32 API has a lot of cases of use of primitive types like a DWORD which
is a simple typedef / type alias of a 32 bit unsigned integer (whose type
varies between platforms! oh my!) where in some contexts is it a holder of
a certain set of bit flags, in another it is a millisecond count, and in
another it is an error code with a specific range.

The wrappers here are _intended_ to be very simple wrapper types that give
some type-wise uniqueness around these primitive types so that they can be
used in overloads and their use in parameter lists is "self documenting"
to some degree.

This is not _intended_ to be the beginning of yet another HANDLE-with-
automatic-CloseHandle-semantics set of classes or such. These are useful
and necessary. There is a set of these provided by the `wil` library, the
Windows Implementation Library, freely and publicly available. `wil` has
its ups and downs and trying to "compete" with it is well outside the
scope of anything I would want to do here with "m".

"m" is intended to move C++ forward, indeally in the context of Windows as
well as other platforms, not move Windows forward in the context of C++.

Unfortunately, it has proven difficult to compose `wil` with "m", so I have
found myself building RAII classes that overlap but they are not part of
the public API surface area of "m" and they are not intended to be for the
foreseeable future.

These types however, are innocuous structs around DWORDs and the like,
just with explicit conversions in and out.

The C++ Guidelines recommend use of a scoped enumeration for these sorts
of things. The problem with scoped enumerations is that you cannot add
some of the operators to them so my default stance is a struct with
explicit constructor and explicit operator T to convert in and out
which should get the basic type safety.

Thus, the default "wrapper" for, for example, a DWORD type might look like:

```
struct dword_for_ms
{
    dword_for_ms() = default;
    constexpr explicit dword_for_ms(DWORD v) noexcept : m_v(v) {}
    constexpr dword_for_ms(dword_for_ms const& other) noexcept : m_v(other.m_v) {}
    constexpr dword_for_ms(dword_for_ms&& other) noexcept : m_v(other.m_v) {}
    ~dword_for_ms() = default;

    constexpr dword_for_ms& operator=(dword_for_ms const& other) noexcept { m_v = other.m_v; return *this; }
    constexpr dword_for_ms& operator=(dword_for_ms&& other) noexcept { m_v = other.m_v; return *this; }

    constexpr void swap(dword_for_ms& other) noexcept {
        using std::swap;
        swap(m_v, other.m_v);
    }

    explicit constexpr operator DWORD() const { return m_v; }
    constexpr bool operator==(dword_for_ms other) const { return m_v == other.m_v; }

    constexpr auto operator<=>(dword_for_ms other) const { return operator<=>(m_v, other.m_v); }

    DWORD m_v;
};
```

Language experts can probably hone this to what really has to be implemented,
for example, perhaps the move constructor should _not_ be implemented.

Also note that this is prime real estate for a template.

