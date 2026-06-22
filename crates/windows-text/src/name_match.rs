// Copyright (c) Microsoft Corporation.

//! Win32 filename wildcard matching (`FsRtlIsNameInExpression` semantics, WT-6).
//!
//! [`name_matches_expression`] decides whether a directory entry `name` matches
//! a search `expression` (the leaf a caller passed to `FindFirstFile`-style
//! enumeration), using Windows **ordinal** casing for the case-insensitive
//! comparison (the same [`OrdinalCasing`] seam the rest of this crate uses, so
//! the matcher is testable off-Windows via the ASCII reference).
//!
//! # Owned specification (Design-Autonomy)
//!
//! We **specify** the matching semantics below and choose [`OrdinalCasing`] to
//! satisfy the case-insensitive comparison; we do not inherit behavior from any
//! dependency. The specification follows Win32 `FsRtlIsNameInExpression`: the
//! expression may contain the literal wildcards `*` and `?`, plus the three DOS
//! metacharacters (illegal in real filenames, so unambiguous):
//!
//! | Token        | Char | Meaning                                                            |
//! |--------------|------|--------------------------------------------------------------------|
//! | `Star`       | `*`  | Zero or more of any character.                                     |
//! | `Qm`         | `?`  | Exactly one of any character.                                      |
//! | `DosStar`    | `<`  | Zero or more characters, but not consuming the final `.` in the name. |
//! | `DosQm`      | `>`  | One character, or zero at end-of-name / immediately before a `.`.  |
//! | `DosDot`     | `"`  | A literal `.`, or zero characters at end-of-name / before a `.`.   |
//!
//! `FsRtlIsNameInExpression` itself does **not** translate a DOS-style pattern;
//! its caller (`FindFirstFile`) does. We fold that translation in so callers can
//! pass an ordinary pattern and get the familiar Win32 quirks. The translation
//! rules (applied to the raw expression) are:
//!
//! 1. A run of `?` immediately before a `.` or the end of the expression becomes
//!    `DosQm` (so trailing `?`s may match fewer characters).
//! 2. A `.` at the end of the expression, or immediately followed by `*` or `?`,
//!    becomes `DosDot` (so `*.*` and a trailing `.` match names with no
//!    extension).
//! 3. A `*` immediately followed by `.` becomes `DosStar` (so `*.ext` anchors on
//!    the *final* `.` of the name).
//!
//! Changing any token meaning or translation rule is a breaking change.
//!
//! An empty expression matches only the empty name. A lone `*` matches any name.

use core::cmp::Ordering;

use crate::casing::OrdinalCasing;

/// `*` — the zero-or-more literal wildcard.
const ASTERISK: u16 = b'*' as u16;
/// `?` — the exactly-one literal wildcard.
const QUESTION: u16 = b'?' as u16;
/// `.` — the extension separator (significant to the DOS metacharacters).
const PERIOD: u16 = b'.' as u16;
/// `<` — the `DOS_STAR` metacharacter (illegal in real filenames).
const DOS_STAR: u16 = b'<' as u16;
/// `>` — the `DOS_QM` metacharacter (illegal in real filenames).
const DOS_QM: u16 = b'>' as u16;
/// `"` — the `DOS_DOT` metacharacter (illegal in real filenames).
const DOS_DOT: u16 = b'"' as u16;

/// A translated expression token (the result of folding the Win32 DOS-pattern
/// translation into the raw expression). See the module docs for semantics.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Token {
    /// A literal code unit, compared via the casing seam.
    Literal(u16),
    /// `*` — zero or more of any character.
    Star,
    /// `?` — exactly one of any character.
    Qm,
    /// `<` — zero or more characters, not consuming the final `.`.
    DosStar,
    /// `>` — one character, or zero at end / before a `.`.
    DosQm,
    /// `"` — a `.`, or zero characters at end / before a `.`.
    DosDot,
}

/// Whether `name` matches the search `expression` under Win32 wildcard
/// semantics (see the module docs). Case-insensitive comparison routes through
/// `casing`; `case_sensitive == true` compares code units verbatim.
///
/// `name` is a directory entry's leaf (it must not itself contain wildcards);
/// `expression` is the search pattern. Both are UTF-16 code-unit slices.
#[must_use]
pub fn name_matches_expression<C: OrdinalCasing>(
    name: &[u16],
    expression: &[u16],
    casing: &C,
    case_sensitive: bool,
) -> bool {
    let tokens = translate(expression);
    match_from(&tokens, 0, name, 0, casing, case_sensitive)
}

/// Fold the Win32 DOS-pattern translation (module rules 1–3) into a token
/// stream, collapsing runs of `*` so backtracking stays bounded.
fn translate(expression: &[u16]) -> Vec<Token> {
    let n = expression.len();
    let mut tokens: Vec<Token> = Vec::with_capacity(n);

    // Pass A: per-character mapping using one-character lookahead (rules 2 & 3).
    for i in 0..n {
        let c = expression[i];
        let next = expression.get(i + 1).copied();
        let token = match c {
            ASTERISK if next == Some(PERIOD) => Token::DosStar, // rule 3
            ASTERISK => Token::Star,
            PERIOD if next.is_none() || next == Some(ASTERISK) || next == Some(QUESTION) => {
                Token::DosDot // rule 2
            }
            PERIOD => Token::Literal(PERIOD),
            QUESTION => Token::Qm, // possibly upgraded to DosQm in pass B
            DOS_STAR => Token::DosStar,
            DOS_QM => Token::DosQm,
            DOS_DOT => Token::DosDot,
            other => Token::Literal(other),
        };
        tokens.push(token);
    }

    // Pass B (rule 1): right-to-left, a `?` belonging to a run that reaches the
    // end of the expression or a `.` becomes `DosQm`. A dot boundary re-arms the
    // run; any other concrete token ends it.
    let mut in_trailing_run = true;
    for token in tokens.iter_mut().rev() {
        match *token {
            Token::Qm => {
                if in_trailing_run {
                    *token = Token::DosQm;
                }
            }
            Token::DosDot | Token::Literal(PERIOD) => in_trailing_run = true,
            _ => in_trailing_run = false,
        }
    }

    // Collapse adjacent `Star`s (e.g. `***` ⇒ one) to bound backtracking.
    let mut collapsed: Vec<Token> = Vec::with_capacity(tokens.len());
    for token in tokens {
        if token == Token::Star && collapsed.last() == Some(&Token::Star) {
            continue;
        }
        collapsed.push(token);
    }
    collapsed
}

/// Compare a single name code unit against a literal expression code unit,
/// honoring the casing seam unless `case_sensitive`.
fn unit_eq<C: OrdinalCasing>(a: u16, b: u16, casing: &C, case_sensitive: bool) -> bool {
    if case_sensitive {
        a == b
    } else {
        casing.compare_ignore_case(&[a], &[b]) == Ordering::Equal
    }
}

/// The backtracking matcher over translated tokens. Non-branching tokens advance
/// iteratively; `Star` / `DosStar` branch by recursion over the name suffix.
fn match_from<C: OrdinalCasing>(
    tokens: &[Token],
    mut ti: usize,
    name: &[u16],
    mut ni: usize,
    casing: &C,
    case_sensitive: bool,
) -> bool {
    loop {
        let Some(token) = tokens.get(ti) else {
            // Expression exhausted: a match iff the name is also exhausted.
            return ni == name.len();
        };

        match *token {
            Token::Star => {
                // Zero or more of any character: try every suffix split.
                for split in ni..=name.len() {
                    if match_from(tokens, ti + 1, name, split, casing, case_sensitive) {
                        return true;
                    }
                }
                return false;
            }
            Token::DosStar => {
                // Zero or more characters, but it may not consume the final `.`
                // of the remaining name.
                let limit = name[ni..]
                    .iter()
                    .rposition(|&c| c == PERIOD)
                    .map_or(name.len(), |dot| ni + dot);
                for split in ni..=limit {
                    if match_from(tokens, ti + 1, name, split, casing, case_sensitive) {
                        return true;
                    }
                }
                return false;
            }
            Token::Qm => {
                // Exactly one of any character.
                if ni < name.len() {
                    ti += 1;
                    ni += 1;
                } else {
                    return false;
                }
            }
            Token::DosQm => {
                // One character, or zero at end / immediately before a `.`.
                if ni < name.len() && name[ni] != PERIOD {
                    ti += 1;
                    ni += 1;
                } else {
                    ti += 1;
                }
            }
            Token::DosDot => {
                // A literal `.`, or zero characters at end / before a `.`.
                if ni < name.len() && name[ni] == PERIOD {
                    ti += 1;
                    ni += 1;
                } else if ni == name.len() || name[ni] == PERIOD {
                    ti += 1;
                } else {
                    return false;
                }
            }
            Token::Literal(expected) => {
                if ni < name.len() && unit_eq(name[ni], expected, casing, case_sensitive) {
                    ti += 1;
                    ni += 1;
                } else {
                    return false;
                }
            }
        }
    }
}
