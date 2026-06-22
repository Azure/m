// Copyright (c) Microsoft Corporation.

//! Unit tests for `windows-text` (CHECKLIST M2-6).

use super::*;
use core::cmp::Ordering;

// ----- Platform-independent: Utf16 storage / egress (D7/D9) -----------------

#[test]
fn utf8_round_trip() {
    for s in [
        "",
        "Software",
        "HKEY_LOCAL_MACHINE",
        "café",
        "日本語",
        "emoji 🦀 here",
    ] {
        let w = Utf16::from_utf8(s);
        assert_eq!(w.to_utf8().unwrap(), s);
    }
}

#[test]
fn ill_formed_utf16_rejected_at_egress() {
    // Lone high surrogate — not well-formed UTF-16.
    let w = Utf16::from_units(vec![0x0041, 0xD800, 0x0042]);
    assert_eq!(w.to_utf8(), Err(Error::IllFormedUtf16));
    // ...but the raw units are preserved losslessly (D9).
    assert_eq!(w.as_units(), &[0x0041, 0xD800, 0x0042]);
    assert_eq!(w.len(), 3);
}

#[test]
fn exact_equality_is_case_sensitive() {
    assert_ne!(Utf16::from_utf8("Foo"), Utf16::from_utf8("foo"));
    assert_eq!(Utf16::from_utf8("Foo"), Utf16::from_utf8("Foo"));
}

// ----- Platform-independent: ASCII reference casing -------------------------

#[test]
fn ascii_reference_equality_is_case_insensitive() {
    let c = AsciiOrdinalCasing;
    let a = Utf16::from_utf8("Software");
    let b = Utf16::from_utf8("SOFTWARE");
    assert_eq!(
        c.compare_ignore_case(a.as_units(), b.as_units()),
        Ordering::Equal
    );
    assert_eq!(c.sort_key(a.as_units()), c.sort_key(b.as_units()));
}

#[test]
fn ascii_reference_orders_ignoring_case() {
    let c = AsciiOrdinalCasing;
    let apple = Utf16::from_utf8("apple");
    let banana = Utf16::from_utf8("Banana");
    assert_eq!(
        c.compare_ignore_case(apple.as_units(), banana.as_units()),
        Ordering::Less
    );
    assert!(c.sort_key(apple.as_units()) < c.sort_key(banana.as_units()));
}

#[test]
fn ascii_reference_sort_key_prefix_orders_before_longer() {
    let c = AsciiOrdinalCasing;
    let short = Utf16::from_utf8("app");
    let long = Utf16::from_utf8("apple");
    assert!(c.sort_key(short.as_units()) < c.sort_key(long.as_units()));
}

// ----- Windows: Win32 ordinal casing (D6/D8) --------------------------------

/// A spread of ASCII strings exercising letters, digits, symbols, and the
/// underscore (a common registry-name character whose ordinal position differs
/// from linguistic collation).
#[cfg(windows)]
const ASCII_SAMPLES: &[&str] = &[
    "",
    "a",
    "A",
    "Z",
    "z",
    "0",
    "9",
    "_",
    "app",
    "apple",
    "Apple",
    "APPLE",
    "Banana",
    "banana",
    "Software",
    "SOFTWARE",
    "HKEY_LOCAL_MACHINE",
    "hkey_local_machine",
    "Value_1",
    "value_2",
    "a_b",
    "ab",
    "ZZTop",
    "zztop",
];

#[cfg(windows)]
#[test]
fn win32_equality_is_case_insensitive() {
    let a = Utf16::from_utf8("HKEY_Local_Machine");
    let b = Utf16::from_utf8("hkey_local_machine");
    assert_eq!(a.compare_ignore_case(&b), Ordering::Equal);
    assert_eq!(a.sort_key(), b.sort_key());
}

#[cfg(windows)]
#[test]
fn win32_ordinal_is_not_linguistic() {
    // Ordinal ignore-case folds 'i' and 'I' to the same key regardless of any
    // Turkish locale (proving the comparison is ordinal, not linguistic).
    let i = Utf16::from_utf8("i");
    let upper_i = Utf16::from_utf8("I");
    assert_eq!(i.compare_ignore_case(&upper_i), Ordering::Equal);

    // The Turkish dotted capital I (U+0130) is a distinct code unit and is not
    // ordinally equal to ASCII 'I'.
    let dotted = Utf16::from_units(vec![0x0130]);
    assert_ne!(dotted.compare_ignore_case(&upper_i), Ordering::Equal);
}

#[cfg(windows)]
#[test]
fn win32_matches_ascii_reference_on_ascii() {
    // Differential parity: on ASCII inputs the Win32 implementation must agree
    // with the pure-Rust ASCII reference, for both comparison and sort-key
    // ordering.
    let reference = AsciiOrdinalCasing;
    for &x in ASCII_SAMPLES {
        for &y in ASCII_SAMPLES {
            let a = Utf16::from_utf8(x);
            let b = Utf16::from_utf8(y);

            assert_eq!(
                a.compare_ignore_case(&b),
                reference.compare_ignore_case(a.as_units(), b.as_units()),
                "compare mismatch for {x:?} vs {y:?}",
            );

            let ka = a.sort_key();
            let kb = b.sort_key();
            assert_eq!(
                ka.cmp(&kb),
                reference
                    .sort_key(a.as_units())
                    .cmp(&reference.sort_key(b.as_units())),
                "sort-key ordering mismatch for {x:?} vs {y:?}",
            );
        }
    }
}

#[cfg(windows)]
#[test]
fn win32_sort_key_agrees_with_compare() {
    // The sort key must reproduce compare_ignore_case under a byte comparison
    // (the OrdinalCasing contract, D8).
    for &x in ASCII_SAMPLES {
        for &y in ASCII_SAMPLES {
            let a = Utf16::from_utf8(x);
            let b = Utf16::from_utf8(y);
            assert_eq!(
                a.sort_key().cmp(&b.sort_key()),
                a.compare_ignore_case(&b),
                "sort-key vs compare mismatch for {x:?} vs {y:?}",
            );
        }
    }
}

#[cfg(windows)]
#[test]
fn win32_ill_formed_utf16_does_not_panic() {
    // Ordinal operations work on code units, so ill-formed UTF-16 must compare
    // and key without panicking (D9).
    let a = Utf16::from_units(vec![0x0041, 0xD800, 0x0042]);
    let b = Utf16::from_units(vec![0x0041, 0xD800, 0x0042]);
    assert_eq!(a.compare_ignore_case(&b), Ordering::Equal);
    let _ = a.sort_key();
}

// ----- Windows: shared golden vectors (WT-5 / M2-7) -------------------------

/// Load the committed golden-vector fixture and assert the Win32 leaf
/// reproduces every recorded sort key and comparator sign. The same fixture is
/// consumed by the C++ PIL suite, pinning cross-language parity of the ordinal
/// key/comparator. Regenerate with
/// `cargo run -p windows-text --example gen_ordinal_golden`.
#[cfg(windows)]
#[test]
fn win32_matches_golden_vectors() {
    use std::fs;

    fn parse_units(field: &str) -> Vec<u16> {
        if field == "-" {
            return Vec::new();
        }
        field
            .as_bytes()
            .chunks(4)
            .map(|c| u16::from_str_radix(std::str::from_utf8(c).unwrap(), 16).unwrap())
            .collect()
    }

    fn parse_bytes(field: &str) -> Vec<u8> {
        if field == "-" {
            return Vec::new();
        }
        field
            .as_bytes()
            .chunks(2)
            .map(|c| u8::from_str_radix(std::str::from_utf8(c).unwrap(), 16).unwrap())
            .collect()
    }

    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/testdata/ordinal_golden_vectors.txt"
    );
    let text = fs::read_to_string(path).expect("golden fixture present");

    let casing = Win32OrdinalCasing;
    let mut inputs: Vec<Vec<u16>> = Vec::new();
    let mut saw_version = false;
    let mut pair_rows = 0usize;

    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut f = line.split_whitespace();
        match f.next().unwrap() {
            "V" => {
                assert_eq!(f.next().unwrap(), "1", "unexpected fixture version");
                saw_version = true;
            }
            "I" => {
                let idx: usize = f.next().unwrap().parse().unwrap();
                assert_eq!(idx, inputs.len(), "I rows must be dense and in order");
                let units = parse_units(f.next().unwrap());
                let expected_key = parse_bytes(f.next().unwrap());
                assert_eq!(
                    casing.sort_key(&units),
                    expected_key,
                    "sort-key mismatch for corpus[{idx}]"
                );
                inputs.push(units);
            }
            "C" => {
                let i: usize = f.next().unwrap().parse().unwrap();
                let j: usize = f.next().unwrap().parse().unwrap();
                let want = match f.next().unwrap() {
                    "lt" => Ordering::Less,
                    "eq" => Ordering::Equal,
                    "gt" => Ordering::Greater,
                    other => panic!("bad sign {other:?}"),
                };
                assert_eq!(
                    casing.compare_ignore_case(&inputs[i], &inputs[j]),
                    want,
                    "comparator mismatch for ({i}, {j})"
                );
                assert_eq!(
                    casing.compare_ignore_case(&inputs[j], &inputs[i]),
                    want.reverse(),
                    "comparator not antisymmetric for ({i}, {j})"
                );
                pair_rows += 1;
            }
            other => panic!("unknown record kind {other:?}"),
        }
    }

    assert!(saw_version, "fixture missing version record");
    assert!(inputs.len() >= 20, "fixture unexpectedly small");
    assert_eq!(
        pair_rows,
        inputs.len() * (inputs.len() - 1) / 2,
        "fixture must contain every i<j comparator pair"
    );
}

// ----- Windows: code-page conversions (M2-5) --------------------------------

#[cfg(windows)]
#[test]
fn code_page_utf8_round_trip() {
    for s in ["", "Software", "café", "日本語", "emoji 🦀 here"] {
        let w = Utf16::from_utf8(s);
        let bytes = w.to_code_page(CodePage::UTF8).unwrap();
        assert_eq!(bytes, s.as_bytes());
        let back = Utf16::from_code_page(CodePage::UTF8, &bytes).unwrap();
        assert_eq!(back, w);
    }
}

#[cfg(windows)]
#[test]
fn code_page_legacy_round_trip() {
    // Windows-1252 round-trips ASCII (and Latin-1 high bytes) losslessly.
    let cp1252 = CodePage::from_id(1252);
    let w = Utf16::from_utf8("Program Files (x86)");
    let bytes = w.to_code_page(cp1252).unwrap();
    let back = Utf16::from_code_page(cp1252, &bytes).unwrap();
    assert_eq!(back, w);
}

#[cfg(windows)]
#[test]
fn code_page_invalid_utf8_is_rejected() {
    // 0xFF is never valid UTF-8; strict decoding must fail rather than
    // substitute a replacement character.
    let result = Utf16::from_code_page(CodePage::UTF8, &[0x41, 0xFF, 0x42]);
    assert!(matches!(result, Err(Error::CodePage { .. })));
}

#[cfg(windows)]
#[test]
fn code_page_empty_round_trips() {
    let empty = Utf16::from_utf8("");
    assert!(empty.to_code_page(CodePage::UTF8).unwrap().is_empty());
    assert_eq!(
        Utf16::from_code_page(CodePage::UTF8, &[]).unwrap(),
        empty
    );
}

// ----- Platform-independent: filename wildcard matching (WT-6) ---------------

/// UTF-16 code units for a `&str` test literal.
fn units(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

/// Case-insensitive match against the ASCII reference casing (the off-Windows
/// seam, WT-4).
fn matches(name: &str, expr: &str) -> bool {
    name_matches_expression(&units(name), &units(expr), &AsciiOrdinalCasing, false)
}

#[test]
fn wildcard_literal_exact_match() {
    assert!(matches("readme.txt", "readme.txt"));
    assert!(!matches("readme.txt", "readme.md"));
    assert!(!matches("readme", "readme.txt"));
}

#[test]
fn wildcard_star_matches_anything() {
    assert!(matches("", "*"));
    assert!(matches("anything.at.all", "*"));
    assert!(matches("no-dot", "*"));
}

#[test]
fn wildcard_star_extension() {
    assert!(matches("a.txt", "*.txt"));
    assert!(!matches("a.dat", "*.txt"));
    // `*.txt` anchors on the FINAL dot, so multi-dot names still match.
    assert!(matches("a.b.txt", "*.txt"));
    assert!(!matches("a.txt.bak", "*.txt"));
}

#[test]
fn wildcard_question_is_exactly_one() {
    assert!(matches("abc", "a?c"));
    assert!(!matches("ac", "a?c"));
    assert!(!matches("abbc", "a?c"));
    // A mid-string `?` matches any single character, including a dot.
    assert!(matches("a.c", "a?c"));
}

#[test]
fn wildcard_prefix_and_suffix() {
    assert!(matches("foobar", "foo*"));
    assert!(!matches("barfoo", "foo*"));
    assert!(matches("barfoo", "*foo"));
    assert!(matches("foobarbaz", "*bar*"));
    assert!(!matches("fooBARbaz", "*qux*"));
}

#[test]
fn wildcard_case_insensitive_by_default() {
    assert!(matches("FOO.TXT", "*.txt"));
    assert!(matches("ReadMe.Txt", "readme.txt"));
}

#[test]
fn wildcard_case_sensitive_flag() {
    assert!(!name_matches_expression(
        &units("FOO.TXT"),
        &units("*.txt"),
        &AsciiOrdinalCasing,
        true,
    ));
    assert!(name_matches_expression(
        &units("foo.txt"),
        &units("*.txt"),
        &AsciiOrdinalCasing,
        true,
    ));
}

#[test]
fn wildcard_empty_expression_matches_only_empty_name() {
    assert!(name_matches_expression(&[], &[], &AsciiOrdinalCasing, false));
    assert!(!matches("x", ""));
}

#[test]
fn wildcard_star_dot_star_matches_extensionless() {
    // The classic Win32 quirk: `*.*` matches names with no extension.
    assert!(matches("foo", "*.*"));
    assert!(matches("foo.txt", "*.*"));
    assert!(matches("", "*.*"));
}

#[test]
fn wildcard_trailing_dot_matches_no_extension() {
    assert!(matches("name", "name."));
    assert!(!matches("name.txt", "name."));
}

#[test]
fn wildcard_trailing_question_run_matches_fewer() {
    // A trailing run of `?` may match fewer characters (DOS_QM).
    assert!(matches("file", "file???"));
    assert!(matches("file1", "file???"));
    assert!(matches("file123", "file???"));
    assert!(!matches("file1234", "file???"));
}

#[test]
fn wildcard_question_run_before_dot() {
    assert!(matches("a.txt", "a???.txt"));
    assert!(matches("abcd.txt", "a???.txt"));
    assert!(!matches("abcde.txt", "a???.txt"));
}

#[test]
fn wildcard_dos_metacharacters_literal_in_expression() {
    // `>` (DOS_QM): one char, or zero before a dot / at end.
    assert!(matches("ab", "a>"));
    assert!(matches("a", "a>"));
    assert!(!matches("abc", "a>"));
    // `"` (DOS_DOT): a dot, or zero at end / before a dot.
    assert!(matches("a.b", "a\"b"));
    assert!(matches("a", "a\""));
    // `<` (DOS_STAR): zero or more, not consuming the final dot.
    assert!(matches("abc.txt", "<.txt"));
    assert!(matches("a.b.txt", "<.txt"));
}

#[test]
fn wildcard_matches_non_bmp_literal() {
    // A non-BMP scalar (🦀, surrogate pair) matches itself literally and via `*`.
    assert!(matches("🦀.txt", "*.txt"));
    assert!(matches("🦀", "🦀"));
    assert!(!matches("🦀", "🦞"));
}

#[test]
fn wildcard_empty_name_against_wildcards() {
    assert!(matches("", "*"));
    // A trailing `?` is DOS_QM, which matches zero characters at end-of-name.
    assert!(matches("", "?"));
    assert!(!matches("ab", "?"));
    assert!(matches("", "*.*"));
}

