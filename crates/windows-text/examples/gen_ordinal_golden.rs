// Copyright (c) Microsoft Corporation.

//! Windows-only generator for the shared ordinal golden-vector fixture
//! (WT-5 / M2-7).
//!
//! Run from the repository root with:
//!
//! ```text
//! cargo run -p windows-text --example gen_ordinal_golden
//! ```
//!
//! It calls the OS string primitives through the `windows-text` safe API
//! (`Win32OrdinalCasing`, which wraps `LCMapStringEx(LCMAP_UPPERCASE)` and
//! `CompareStringOrdinal`) over a fixed corpus and writes
//! `testdata/ordinal_golden_vectors.txt`. The output is the authoritative,
//! curated definition of the ordinal sort key and the case-insensitive
//! comparator (D6/D8). Both the Rust and the C++ PIL test suites load this
//! same file and assert their implementations reproduce it exactly.
//!
//! The corpus deliberately covers ASCII letters/digits/`_`/punctuation,
//! mixed-case words, the `_` / `a_b` vs `ab` cases that distinguish an ordinal
//! key from a linguistic one (WT-2), BMP non-ASCII (including U+0130/U+0131),
//! and ill-formed UTF-16 (lone surrogates, D9).
//!
//! Curation: as it emits each comparator row the generator asserts that the
//! comparator sign agrees with the byte ordering of the two sort keys (the D8
//! contract). If the OS ever violated that invariant the generator would panic
//! here, forcing an explicit decision rather than silently committing a quirk.

#[cfg(windows)]
fn main() -> std::io::Result<()> {
    use core::cmp::Ordering;
    use windows_text::{OrdinalCasing, Win32OrdinalCasing};

    // Build one UTF-16 corpus entry from a `&str`.
    fn s(text: &str) -> Vec<u16> {
        text.encode_utf16().collect()
    }

    // Fixed corpus. Order defines the indices referenced by the comparator
    // rows, so it must never be reordered without regenerating the fixture.
    let corpus: Vec<Vec<u16>> = vec![
        s(""),                    // 0  empty
        s("a"),                   // 1  ASCII lower
        s("A"),                   // 2  ASCII upper
        s("Z"),                   // 3
        s("z"),                   // 4
        s("0"),                   // 5  digit
        s("9"),                   // 6  digit
        s("_"),                   // 7  underscore (U+005F)
        s("."),                   // 8  punctuation
        s("ab"),                  // 9  ordinal-vs-linguistic pair: case-folded
        s("a_b"),                 // 10 `_`(005f) > `B`(0042), so a_b sorts after ab
        s("apple"),               // 11 mixed-case word group
        s("Apple"),               // 12
        s("APPLE"),               // 13
        s("Banana"),              // 14
        s("banana"),              // 15
        s("HKEY_LOCAL_MACHINE"),  // 16 registry-style names
        s("hkey_local_machine"),  // 17
        s("Value_1"),             // 18
        s("value_2"),             // 19
        s("ZZTop"),               // 20
        s("zztop"),               // 21
        s("i"),                   // 22 dotted/dotless I family
        s("I"),                   // 23
        s("café"),                // 24 BMP non-ASCII (U+00E9)
        s("CAFÉ"),                // 25 (U+00C9)
        s("ß"),                   // 26 sharp s (U+00DF)
        vec![0x0130],             // 27 İ LATIN CAPITAL I WITH DOT ABOVE
        vec![0x0131],             // 28 ı LATIN SMALL DOTLESS I
        vec![0x0041, 0xD800, 0x0042], // 29 ill-formed: lone high surrogate
        vec![0xDC00],             // 30 ill-formed: lone low surrogate
    ];

    let casing = Win32OrdinalCasing;
    let keys: Vec<Vec<u8>> = corpus.iter().map(|u| casing.sort_key(u)).collect();

    fn hex_units(units: &[u16]) -> String {
        if units.is_empty() {
            return "-".to_string();
        }
        units.iter().map(|u| format!("{u:04x}")).collect()
    }

    fn hex_bytes(bytes: &[u8]) -> String {
        if bytes.is_empty() {
            return "-".to_string();
        }
        bytes.iter().map(|b| format!("{b:02x}")).collect()
    }

    fn sign(ord: Ordering) -> &'static str {
        match ord {
            Ordering::Less => "lt",
            Ordering::Equal => "eq",
            Ordering::Greater => "gt",
        }
    }

    let mut out = String::new();
    out.push_str("# windows-text \u{2014} ordinal golden vectors (WT-5 / M2-7)\r\n");
    out.push_str("# DO NOT EDIT BY HAND. Regenerate with:\r\n");
    out.push_str("#   cargo run -p windows-text --example gen_ordinal_golden\r\n");
    out.push_str("# Authoritative, curated definition of the ordinal sort key and\r\n");
    out.push_str("# case-insensitive comparator (D6/D8). Loaded and asserted by both the\r\n");
    out.push_str("# Rust and the C++ PIL test suites.\r\n");
    out.push_str("#\r\n");
    out.push_str("# Grammar (one record per line; blank lines and `#` comments ignored):\r\n");
    out.push_str("#   V <version>\r\n");
    out.push_str("#   I <index> <input-hex> <sortkey-hex>\r\n");
    out.push_str("#   C <i> <j> <sign>          sign in { lt, eq, gt }\r\n");
    out.push_str("# <input-hex>: big-endian u16 code units as lowercase hex, or `-` if empty.\r\n");
    out.push_str("# <sortkey-hex>: raw key bytes as lowercase hex, or `-` if empty.\r\n");
    out.push_str("# A `C i j` row records compare_ignore_case(corpus[i], corpus[j]).\r\n");
    out.push_str("V 1\r\n");

    for (i, units) in corpus.iter().enumerate() {
        out.push_str(&format!(
            "I {i} {} {}\r\n",
            hex_units(units),
            hex_bytes(&keys[i])
        ));
    }

    for i in 0..corpus.len() {
        for j in (i + 1)..corpus.len() {
            let cmp = casing.compare_ignore_case(&corpus[i], &corpus[j]);
            // Curation invariant (D8): the comparator must agree with the byte
            // ordering of the materialized sort keys. A divergence here is an OS
            // quirk we have NOT agreed to adopt; fail loudly instead of baking
            // it into the committed fixture.
            assert_eq!(
                cmp,
                keys[i].cmp(&keys[j]),
                "sort-key/comparator divergence for corpus[{i}] vs corpus[{j}]"
            );
            out.push_str(&format!("C {i} {j} {}\r\n", sign(cmp)));
        }
    }

    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("testdata");
    std::fs::create_dir_all(&dir)?;
    let path = dir.join("ordinal_golden_vectors.txt");
    std::fs::write(&path, out.as_bytes())?;
    println!("wrote {}", path.display());
    Ok(())
}

#[cfg(not(windows))]
fn main() {
    eprintln!("gen_ordinal_golden only runs on Windows (it calls Win32 string APIs).");
}
