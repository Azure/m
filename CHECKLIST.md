# x64-release-clang: Clean Build & Test Pass Checklist

## Status

- [x] **Configure** — `cmake --preset x64-release-clang` completes successfully
- [x] **Build** — `cmake --build out/build/x64-release-clang` completes (362/362 targets, 0 errors)
- [ ] **Tests pass** — `ctest --test-dir out/build/x64-release-clang` (98% pass, **18 failing**, down from 31)

---

## Failing Tests — Grouped by Root Cause

### Group 1 · `m::math` library — wrong logic in `math.h` (22 failures)

These tests live in `src/libraries/math/test/`.

#### 1a · `(unsigned – unsigned → unsigned)` subtract: spurious overflow check

**Failing tests:**

- [x] `SubtractionUnsignedUnsigned.BasicSubtraction`
- [x] `SubtractionUnsignedUnsigned.MaxValueEdgeCases`
- [x] `SubtractionUnsignedUnsigned.DifferentSizedTypes`
- [x] `SubtractionUnsignedUnsigned.NarrowingResults`
- [x] `SubtractionAllSizes.UInt8Subtraction`
- [x] `SubtractionAllSizes.UInt16Subtraction`
- [x] `SubtractionAllSizes.UInt64Subtraction`
- [x] `SubtractionEdgeCases.SubtractSelf`

**Root cause:**  
`safe_math_helper<unsigned, unsigned, unsigned>::subtract` contains a copy-paste of the addition overflow guard:

```cpp
if ((rv < lmax) || (rv < rmax))   // ← wrong: copied from add(), always fires for a > 0
    throw std::overflow_error("…result does not fit…");
```

After the prior guard `if (r > l) throw`, `rv = lmax - rmax` is always ≤ `lmax`, so `rv < lmax` is true whenever `r > 0`. The check fires on every valid subtraction.

**Fix:** Remove the two-condition check; the sole remaining safety net is `m::try_cast<ResultT>(rv)`, which already throws when the result doesn't fit in `ResultT`.

---

#### 1b · `(unsigned – signed → signed)` subtract: calls `add` for positive `r`

**Failing tests:**

- [x] `SubtractionUnsignedSignedToSigned.PositiveSigned`
- [x] `SubtractionUnsignedSignedToSigned.NegativeSigned`

**Root cause:**  
`safe_math_helper<unsigned, signed, signed>::subtract` computes `r_as_unsigned = |r|` regardless of the sign of `r`, then always calls `add`:

```cpp
auto r_as_unsigned = static_cast<uintmax_t>((r < 0) ? (-r) : r);
return safe_math_helper<LeftT, uintmax_t, ResultT>::add(l, r_as_unsigned);  // ← always adds
```

For `subtract(100u, 50, int32_t{})` this computes `100 + 50 = 150` instead of `100 − 50 = 50`.

**Fix:** Branch on `r`:
- `r < 0` → `l − r = l + |r|` → delegate to `add`
- `r ≥ 0` → `l − r` → delegate to `safe_math_helper<LeftT, uintmax_t, ResultT>::subtract`

Handle the `r == RightT::min()` edge case (cannot safely negate) first.

---

#### 1c · `(signed – unsigned → signed)` subtract: computes `r + |l|` instead of `l − r`

**Failing tests:**

- [x] `SubtractionSignedUnsignedToSigned.PositiveSigned`
- [x] `SubtractionSignedUnsignedToSigned.NegativeSigned`

**Root cause:**  
`safe_math_helper<signed, unsigned, signed>::subtract` computes `l_as_unsigned = |l|` and then unconditionally calls `safe_math_helper<RightT, uintmax_t, ResultT>::add(r, l_as_unsigned)` — i.e. `r + |l|` — regardless of sign:

```cpp
auto l_as_unsigned = static_cast<uintmax_t>((l < 0) ? (-l) : l);
return safe_math_helper<RightT, uintmax_t, ResultT>::add(r, l_as_unsigned);  // ← wrong
```

For `subtract(100, 50u, int32_t{})` this computes `50 + 100 = 150` instead of `50`.

**Fix:** Implement the full case split:
- `l ≥ 0`: if `l_as_unsigned ≥ r` → positive result via `try_cast`; else → negate `(r − l_as_unsigned)` using the `unary_safe_math_helper<uintmax_t, ResultT>::negate` helper.
- `l < 0`: result is always `−(|l| + r)`; guard against `uintmax_t` overflow of the sum, then negate.

The pre-existing early-throw for `l == intmax_t::min()` is too broad; replace it with the magnitude-based negate described above.

---

#### 1d · `(unsigned × signed → unsigned)` multiply: doesn't throw when `l == 0` and `r < 0`

**Failing test:**

- [x] `MultiplicationUnsignedSignedToUnsigned.NegativeMultiplier`

**Root cause (CORRECTED):**  
The design rule is that all arithmetic is computed in the abstract integer set ℤ and the result is then mapped to the result type. `0 × (−1) = 0` in ℤ, and `0` is representable in `uint32_t`, so the result is `0` — no overflow. The library implementation correctly returns `0` via the early-exit `if (l == 0) return 0;`. The test was wrong to expect `std::overflow_error` for `multiply(uint32_t{0}, int32_t{-1}, uint32_t{})`.

**Fix:** Change the test assertion for `multiply(uint32_t{0}, int32_t{-1}, uint32_t{})` from `EXPECT_THROW` to `EXPECT_EQ(..., 0u)`. ✓ (test fixed)

---

#### 1e · `(signed + unsigned → unsigned)` add: overflow not detected for equal-sized types ✓

**Test:**

- [x] `SignedUnsignedToUnsigned.AdditionPositiveOverflow`

**Status:** Now passing. `m::try_cast<ResultT>(sum)` correctly throws when `sum` exceeds `ResultT::max`.

---

#### 1f · `(unsigned + signed → signed)` and `(signed + unsigned → signed)` add: throws when negative operand dominates

**Failing tests:**

- [ ] `AdditionUnsignedSignedToSigned.NegativeSigned`
- [ ] `AdditionSignedUnsignedToSigned.NegativeSigned`

**Observed:** `add(uint32_t{50}, int32_t{-100}, int32_t{})` throws `"integer overflow"` instead of returning `-50`.

**Root cause:**  
Both `safe_math_helper<unsigned, signed, signed>::add` and `safe_math_helper<signed, unsigned, signed>::add` contain this guard in their negative-operand path:

```cpp
uintmax_t that_which_remains = static_cast<uintmax_t>(-promoted_r);  // or -promoted_l
if (that_which_remains > promoted_l)     // or promoted_r
    throw std::overflow_error("integer overflow");   // ← wrong: fires when result is negative
```

When the negative operand's magnitude exceeds the positive operand (e.g. `|-100| > 50`), the mathematical result is negative (`50 + (−100) = −50`). This is **valid** for a signed `ResultT` — but the guard unconditionally throws instead of computing and negating the difference.

**Fix:** In both helpers, replace the unconditional throw with a negative-result branch:

```cpp
if (that_which_remains > promoted_l) {
    // result is negative: -(that_which_remains - promoted_l)
    return unary_safe_math_helper<uintmax_t, ResultT>::negate(
        that_which_remains - promoted_l);
}
promoted_l -= that_which_remains;
return m::try_cast<ResultT>(promoted_l);
```

`unary_safe_math_helper<uintmax_t, ResultT>::negate` already handles the full overflow check against `ResultT::min` and the `INT_MIN` special case.

---

#### 1g · `(signed + signed → signed)` add: intermediate computed in `common_type_t` instead of `intmax_t`

**Failing tests:**

- [ ] `SignedSignedArithmetic.DifferentSizedTypes`
- [ ] `IntermediateOverflow.SignedSmallToLargeSucceeds` (see also 1i)

**Observed:** `add(int8_t{127}, int8_t{1}, int32_t{})` throws `"integer overflow"` instead of returning `128`.

**Root cause:**  
`safe_math_helper<signed, signed, signed>::add` uses `common_type_t = std::common_type_t<LeftT, RightT>` for its promoted types and overflow guards:

```cpp
using common_type_t = std::common_type_t<LeftT, RightT>;   // int8_t for <int8_t, int8_t>!

auto promoted_l = static_cast<common_type_t>(l);            // int8_t{127}
auto promoted_r = static_cast<common_type_t>(r);            // int8_t{1}

constexpr auto max_common = (std::numeric_limits<common_type_t>::max)();  // 127

if (promoted_r > 0 && promoted_l > max_common - promoted_r)  // 127 > 127-1=126 → TRUE → throws
    throw std::overflow_error("integer overflow");
```

`std::common_type_t<T, T>` is `T` itself (not `int`). For `int8_t + int8_t`, the guard fires against `int8_t::max = 127` even though `ResultT = int32_t` can represent `128` without difficulty. The library's stated design—*compute in Z, then check against ResultT*—requires the intermediate to be at least as wide as `ResultT`, not as wide as the inputs.

**Fix:** Replace `common_type_t` with `intmax_t` for all promoted arithmetic in this specialization's `add` and `subtract`:

```cpp
auto promoted_l = static_cast<intmax_t>(l);
auto promoted_r = static_cast<intmax_t>(r);
constexpr auto max_val = (std::numeric_limits<intmax_t>::max)();
constexpr auto min_val = (std::numeric_limits<intmax_t>::min)();

if (promoted_r > 0 && promoted_l > max_val - promoted_r)
    throw std::overflow_error("integer overflow");
if (promoted_r < 0 && promoted_l < min_val - promoted_r)
    throw std::overflow_error("integer overflow");

intmax_t const rv = promoted_l + promoted_r;
return m::try_cast<ResultT>(rv);   // this catches the real ResultT overflow
```

The guards now only fire for true `intmax_t` overflow (astronomically rare), and `m::try_cast<ResultT>` is the correct final arbiter of whether the result fits in `ResultT`.

---

#### 1h · `(unsigned ÷ signed → signed)` divide: **test bug** — `large` overflows `uint32_t` to `0`

**Failing test:**

- [ ] `DivisionUnsignedSignedToSigned.IntMinDivisor`

**Observed:** `divide(large, int32_t{INT32_MIN}, int32_t{})` returns `0`, expected `-2`.

**Root cause (test bug):**  
The test computes:

```cpp
uint32_t large = static_cast<uint32_t>(-(static_cast<int64_t>(min32))) * 2;
//              = static_cast<uint32_t>(2147483648) * 2
//              = uint32_t{2147483648} * 2
//              = 4294967296 % 2^32 = 0   ← wraps to zero!
```

`2 × |INT32_MIN| = 4294967296` exceeds `UINT32_MAX = 4294967295`, so `large` silently wraps to `0`. `divide(0, INT32_MIN, int32_t{})` correctly returns `0`, not `−2`. The implementation is correct; the test is wrong.

A `uint32_t` dividend divided by `INT32_MIN` (= `−2147483648`) can yield at most `−1` (when the dividend equals `UINT32_MAX`), because `UINT32_MAX / 2147483648 ≈ 1.999…` truncates toward zero to `1`, giving `−1` with a negative divisor. The quotient `−2` requires a dividend ≥ `4294967296`, which is beyond `uint32_t` range.

**Fix:** Replace the unreachable `-2` case with a reachable assertion:

```cpp
// Old (broken): expects -2 from a dividend that wraps to 0
uint32_t large = static_cast<uint32_t>(-(static_cast<int64_t>(min32))) * 2;
EXPECT_EQ(m::math::divide(large, min32, int32_t{}), -2);

// New (correct): UINT32_MAX / INT32_MIN truncates to -1
EXPECT_EQ(m::math::divide(std::numeric_limits<uint32_t>::max(), min32, int32_t{}), -1);
```

---

#### 1i · `IntermediateOverflow.SignedSmallToLargeSucceeds`: unexpected overflow (same root cause as 1g)

**Failing test:**

- [ ] `IntermediateOverflow.SignedSmallToLargeSucceeds`

**Observed:** `add(int8_t{100}, int8_t{100}, int16_t{})` throws instead of returning `200`.

**Root cause:** Identical to **1g**. The test exercises `(signed + signed → wider-signed)` with small input types. `std::common_type_t<int8_t, int8_t>` = `int8_t`, and the overflow guard fires against `int8_t::max = 127` before the result is ever checked against `int16_t::max = 32767`. This is an independent manifestation of the same defect, not a secondary consequence of 1a–1h.

**Fix:** Same as 1g — use `intmax_t` for the intermediate in `safe_math_helper<signed, signed, signed>::add` and `subtract`.

---

#### 1j · `ExerciseUnsignedAdd` — test bug: `rdigits + 1` CHEATING causes wrong branch selection

**Failing tests:**

- [ ] `ExerciseUnsignedAdd.Add_uint64_int64_to_uint64`
- [ ] `ExerciseUnsignedAdd.Add_uint32_int32_to_uint32`
- [ ] `ExerciseUnsignedAdd.Add_uint64_int32_to_uint32`
- [ ] `ExerciseUnsignedAdd.Add_uint32_int64_to_uint64`

**Observed:** `add(uint64_t{1}, int64_t{INT64_MAX}, uint64_t{})` does not throw, but the test expects `std::overflow_error`. The sum `1 + INT64_MAX = 2^63` fits in `uint64_t` (max `2^64 − 1`), so the implementation is correct.

**Root cause (test bug):**  
`exercise_add` intentionally "cheats" `rdigits` upward:

```cpp
constexpr auto rdigits = std::numeric_limits<RightType>::digits + 1; // CHEATING!! :-)
```

For `RightType = int64_t`, `digits = 63`, so `rdigits = 64`. This makes the test treat `int64_t` as occupying the full 64-bit space (same as `uint64_t`), causing it to enter an `rdigits == sdigits` branch that asserts `EXPECT_THROW` for `add(l_one, r_greatest, SumType{})`. But `1 + INT64_MAX = 2^63`, which is representable in `uint64_t` — no overflow occurs and none should.

Without the `+1` cheat:
- For `Add_uint64_int64_to_uint64`: `rdigits = 63 < sdigits = 64` → enters the `(ldigits == sdigits) && (rdigits < sdigits)` **empty** branch — no problematic assertions.
- For `Add_uint32_int32_to_uint32`: `rdigits = 31 < sdigits = 32` → same empty branch.
- For `Add_uint64_int32_to_uint32`: `rdigits = 31 < sdigits = 32` → enters `(ldigits > sdigits) && (rdigits < sdigits)` → only `EXPECT_EQ` assertions (no spurious throw expectations).
- For `Add_uint32_int64_to_uint64`: `rdigits = 63 < sdigits = 64` → enters `(ldigits < sdigits) && (rdigits < sdigits)` → only `EXPECT_EQ` assertions.

All four tests pass without the `+1`. No currently-passing test is affected (verified by tracing each of the seven currently-passing `ExerciseUnsignedAdd` variants through the digit comparisons without the cheat).

**Fix:** Remove the `+ 1` from `rdigits` in `add_unsigned_signed_to_unsigned.cpp`:

```cpp
// Before (buggy):
constexpr auto rdigits = std::numeric_limits<RightType>::digits + 1; // CHEATING!! :-)

// After (correct):
constexpr auto rdigits = std::numeric_limits<RightType>::digits;
```

---

### Group 2 · `m::bitset` — `popcount()` counts only 32 bits per 64-bit chunk (1 failure)

**Failing test:**

- [ ] `TestBitset.PopCount2`

**Root cause:**  
`bitset::popcount()` folds with a lambda whose parameter is typed as `std::size_t rep`:

```cpp
return std::ranges::fold_left(
    m_bits, std::size_t{}, [](std::size_t acc, std::size_t rep) {
        return acc + std::popcount(rep);
    });
```

`m_bits` is `std::array<uint64_t, N>`. Under clang-cl targeting Windows x64, `std::size_t` maps to `unsigned __int64`, which is a distinct type from `unsigned long long` (`uint64_t`). Clang's `std::popcount` is only overloaded for the standard unsigned integer types; `unsigned __int64` triggers implicit conversion to the nearest standard type — `unsigned int` (32-bit) — silently truncating the upper 32 bits of each 64-bit chunk. This explains the observed result of exactly half the expected count.

**Fix:**  
Change the lambda parameter from `std::size_t rep` to `representation_type rep`:

```cpp
return std::ranges::fold_left(
    m_bits, std::size_t{}, [](std::size_t acc, representation_type rep) {
        return acc + std::popcount(rep);
    });
```

`representation_type` = `uint64_t` = `unsigned long long`, which is a standard unsigned integer type with a correct `std::popcount` overload.

---

### Group 3 · Thread description / timer / work queue — access violations under clang-cl release (4 failures)

**Failing tests:**

- [ ] `ThreadDescription.BasicThreadDescription`
- [ ] `ThreadDescription.Scopes1` — SEH `0xc0000005` (access violation)
- [ ] `Timer.Wait100msTwiceWithDescription`
- [ ] `WorkQueue.QueueWithDescriptions` — SEGFAULT

**Root cause:**  
All four tests use the `m::thread_description` RAII type (or indirectly call it from the timer/work-queue with-description APIs). Under clang-cl with release optimizations, the `set_thread_description` / `restore_thread_description` platform implementation (`src/libraries/thread_description/src/Platforms/windows/`) is either:

- Accessing a null `SetThreadDescription` function pointer (loaded via `GetProcAddress` but not guarded for older OS versions), **or**
- Using a `saved_thread_state` array that holds `void*` slots sized for the MSVC ABI but whose layout or initialization diverges under clang-cl's calling convention or optimization settings.

The `Scopes1` test calls `::LocalFree(pwsz)` on a `PWSTR pwsz{}` (value-initialized to `nullptr`) when `GetThreadDescription` fails or returns a null string — `LocalFree(nullptr)` is documented as a no-op on Windows, but only if the crash isn't in `GetThreadDescription` itself.

**Fix:**

1. Inspect `src/libraries/thread_description/src/Platforms/windows/` for the `SetThreadDescription` / `GetThreadDescription` function pointer acquisition — guard against the API being unavailable (Windows < 1607 / Server 2016).
2. Verify the `saved_thread_state` array size and layout assumptions hold under clang-cl; add a `static_assert` on the struct size.
3. If the crash is in `restore_thread_description`, check that the saved state is never used after its lifetime has ended (use-after-free under aggressive inlining).

---

### Group 4 · Filesystem monitor — callbacks never fire in release build (3 failures)

**Failing tests:**

- [ ] `Monitor.MonitorNonExistentFile`
- [ ] `Monitor.MonitorNonExistentSubdirectory`
- [ ] `Monitor.MonitorFileLifecycle`

**Root cause:**  
The monitor tests set up a `directory_watcher` with a short timer and then wait for `m_on_directory_access_failure_count` (or file change/delete counts) to reach an expected value. Under the release-clang build, the counter stays at 0. Likely causes:

- A callback invocation is dead-code-eliminated by the release optimizer if the lambda capture or virtual dispatch is deemed "no observable effect" (possible if the callback stores to a local or the observer pointer is elided).
- A race condition between the timer firing and the test checking the count that is masked in debug builds by slower execution.
- The `directory_watcher` constructor or timer start fails silently — verify return values / `HRESULT`s are checked.

**Fix:**

1. Mark counters in the test's consumer struct `volatile` or use `std::atomic` so the optimizer cannot elide the stores.
2. Add a timeout + spin-wait loop (or a `std::latch`) in the test to give the async callback adequate time before asserting.
3. Check whether `directory_watcher` on a non-existent path returns an error that prevents the watcher from ever starting, and add an assertion/guard in the constructor for that case.

---

### Group 5 · OVERLAPPED formatter — handle prints as 32-bit hex (1 failure)

**Failing test:**

- [ ] `OVERLAPPED.first`

**Observed vs. expected:**

```
Got:      "… Handle: 0xffffffff"
Expected: "… Handle: 0xffffffffffffffff"
```

**Root cause:**  
`src/Windows/libraries/formatters/include/m/formatters/OVERLAPPED.h` formats the event handle as:

```cpp
reinterpret_cast<uintptr_t>(o.hEvent)
```

Under clang-cl on Windows x64, `HANDLE` (`void*`) is 64-bit but `uintptr_t` may resolve to `unsigned long` (32-bit under the LLP64 model in some clang headers), silently truncating the upper 32 bits. `INVALID_HANDLE_VALUE = (HANDLE)(LONG_PTR)-1 = 0xffffffffffffffff` becomes `0xffffffff`.

**Fix:**  
Cast to `uint64_t` explicitly (which is always 64-bit) rather than `uintptr_t`:

```cpp
static_cast<uint64_t>(reinterpret_cast<uintptr_t>(o.hEvent))
// or more directly:
reinterpret_cast<uint64_t>(o.hEvent)
```

Alternatively, use `{:016x}` to force zero-padding to 16 hex digits regardless of the value's type width.

---

## Execution Order

Steps 1–6 are already done. Remaining work in recommended order:

- [x] **Group 1a** — `(unsigned – unsigned → unsigned)` subtract: remove wrong overflow check ✓
- [x] **Group 1b** — `(unsigned – signed → signed)` subtract: add sign branch ✓
- [x] **Group 1c** — `(signed – unsigned → signed)` subtract: full rewrite ✓
- [x] **Group 1d** — `(unsigned × signed → unsigned)` multiply: test corrected to `EXPECT_EQ(..., 0u)` ✓
- [x] **Group 1e** — `SignedUnsignedToUnsigned.AdditionPositiveOverflow`: now passing ✓
- [ ] **Group 5** — OVERLAPPED formatter: change `uintptr_t` cast to `uint64_t` (1-line fix)
- [ ] **Group 2** — `bitset::popcount`: change lambda param from `std::size_t` to `uint64_t` (1-line fix)
- [ ] **Group 1j** — `ExerciseUnsignedAdd` test: remove `+ 1` from `rdigits` (1-line fix)
- [ ] **Group 1h** — `DivisionUnsignedSignedToSigned.IntMinDivisor` test: fix `large` overflow, change expected value to `-1`
- [ ] **Group 1g / 1i** — `(signed + signed → signed)` add/subtract: replace `common_type_t` with `intmax_t` in intermediate; fixes `DifferentSizedTypes` and `SignedSmallToLargeSucceeds`
- [ ] **Group 1f** — add negative-result branch in `(unsigned+signed→signed)` and `(signed+unsigned→signed)` add
- [ ] **Group 3** — Thread description / timer / work queue access violations
- [ ] **Group 4** — Filesystem monitor callback / timing issues

---

## Verification Steps (required after all fixes)

```bash
# Clean configure
Remove-Item -Recurse -Force out\build\x64-release-clang

cmake --preset x64-release-clang

# Full rebuild
cmake --build out\build\x64-release-clang

# All tests must pass
ctest --test-dir out\build\x64-release-clang --output-on-failure
```

Work is **not complete** until `ctest` reports **0 tests failed**.


