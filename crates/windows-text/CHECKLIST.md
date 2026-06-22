# windows-text — CHECKLIST

## WTM — Win32 filename wildcard matching (`FsRtlIsNameInExpression`, WT-6)

Adds the ordinal filename-matching primitive that pairs with the existing
`OrdinalCasing` seam. Behavior is owned by us (Design Autonomy): we **specify**
Win32 DOS wildcard semantics and use the in-crate `OrdinalCasing` for the
case-insensitive comparison, so the matcher is testable off-Windows via
`AsciiOrdinalCasing` (WT-4). First consumer is the `windows-win32-shim` find
family; the primitive is general (any name-vs-pattern matching).

- [x] **WTM-1** Add a `name_match` module exposing
      `pub fn name_matches_expression<C: OrdinalCasing>(name: &[u16],
      expression: &[u16], casing: &C, case_sensitive: bool) -> bool`
      implementing Win32 DOS wildcard semantics over UTF-16 code units:
      `*` (zero-or-more), `?` (exactly-one, but matches zero at end / before a
      `.`), and the DOS meta-expansions `<` (`DOS_STAR`), `>` (`DOS_QM`),
      `"` (`DOS_DOT`) that Win32 substitutes for trailing `*` / `?` / `.`.
      Case-insensitive matching routes single code-unit compares through
      `OrdinalCasing::compare_ignore_case`; `case_sensitive == true` compares
      code units verbatim. Record **WT-6** in `DESIGN-NOTES.md` (note: changing
      the metacharacter mapping is a breaking change) and add it to the decision
      index. Unit tests (using `AsciiOrdinalCasing`): ≥10 normal cases (`*.txt`,
      `a?c`, literal, `*`, empty expression, leading/trailing wildcards, mixed
      case) plus edge cases (trailing `?`/`.`, empty name, surrogate-pair units,
      the `<`/`>`/`"` meta-expansions, `*` matching across `.`).

      > **➡ CROSS-COMPONENT HANDOFF:** the first consumer is
      > `windows-win32-shim` → `MW8` → `MW8-1`. See
      > [`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md).
