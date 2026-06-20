// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Minimal, dependency-free NDJSON I/O for flat records.
//!
//! Records are one JSON object per line. We deliberately avoid a serialization
//! crate so the tool builds with `std` only on any platform. The parser handles
//! exactly the shapes this tool emits — flat objects whose values are strings,
//! unsigned integers, booleans, or arrays of strings — and nothing more.

use std::collections::BTreeMap;
use std::fmt::Write as _;

/// A builder for one compact JSON object line.
pub struct ObjectWriter {
    buf: String,
    first: bool,
}

impl ObjectWriter {
    pub fn new() -> Self {
        ObjectWriter {
            buf: String::from("{"),
            first: true,
        }
    }

    fn sep(&mut self) {
        if self.first {
            self.first = false;
        } else {
            self.buf.push(',');
        }
    }

    pub fn str_field(&mut self, key: &str, val: &str) -> &mut Self {
        self.sep();
        let _ = write!(self.buf, "\"{}\":\"{}\"", key, escape(val));
        self
    }

    pub fn u64_field(&mut self, key: &str, val: u64) -> &mut Self {
        self.sep();
        let _ = write!(self.buf, "\"{}\":{}", key, val);
        self
    }

    pub fn bool_field(&mut self, key: &str, val: bool) -> &mut Self {
        self.sep();
        let _ = write!(self.buf, "\"{}\":{}", key, val);
        self
    }

    pub fn str_array_field(&mut self, key: &str, vals: &[String]) -> &mut Self {
        self.sep();
        let _ = write!(self.buf, "\"{}\":[", key);
        for (i, v) in vals.iter().enumerate() {
            if i != 0 {
                self.buf.push(',');
            }
            let _ = write!(self.buf, "\"{}\"", escape(v));
        }
        self.buf.push(']');
        self
    }

    /// Finish the object, returning the JSON line (without a trailing newline).
    pub fn finish(mut self) -> String {
        self.buf.push('}');
        self.buf
    }
}

impl Default for ObjectWriter {
    fn default() -> Self {
        Self::new()
    }
}

fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out
}

/// A parsed JSON scalar/array value (only the shapes this tool emits).
///
/// The accessor surface is intentionally complete (strings, numbers, booleans,
/// arrays) even though the current readers do not consume every shape — keeping
/// the parser able to accept any line this tool writes, including header arrays.
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub enum Value {
    Str(String),
    /// Numbers are kept as their textual form so 64-bit bit patterns survive
    /// without precision loss; use [`Value::as_u64`] to decode.
    Num(String),
    Bool(bool),
    Arr(Vec<String>),
}

impl Value {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::Str(s) => Some(s),
            _ => None,
        }
    }
    pub fn as_u64(&self) -> Option<u64> {
        match self {
            Value::Num(s) => s.parse::<u64>().ok(),
            _ => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Value::Bool(b) => Some(*b),
            _ => None,
        }
    }
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn as_arr(&self) -> Option<&[String]> {
        match self {
            Value::Arr(a) => Some(a),
            _ => None,
        }
    }
}

/// Parse one flat JSON object line into a key→value map. Returns `None` if the
/// line is not a well-formed flat object in the restricted grammar.
pub fn parse_object(line: &str) -> Option<BTreeMap<String, Value>> {
    let b = line.trim().as_bytes();
    let mut p = Parser { b, i: 0 };
    p.skip_ws();
    if p.peek()? != b'{' {
        return None;
    }
    p.i += 1;
    let mut map = BTreeMap::new();
    p.skip_ws();
    if p.peek()? == b'}' {
        return Some(map);
    }
    loop {
        p.skip_ws();
        let key = p.parse_string()?;
        p.skip_ws();
        if p.peek()? != b':' {
            return None;
        }
        p.i += 1;
        p.skip_ws();
        let val = p.parse_value()?;
        map.insert(key, val);
        p.skip_ws();
        match p.peek()? {
            b',' => {
                p.i += 1;
                continue;
            }
            b'}' => {
                break;
            }
            _ => return None,
        }
    }
    Some(map)
}

struct Parser<'a> {
    b: &'a [u8],
    i: usize,
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<u8> {
        self.b.get(self.i).copied()
    }

    fn skip_ws(&mut self) {
        while let Some(c) = self.peek() {
            if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
                self.i += 1;
            } else {
                break;
            }
        }
    }

    fn parse_string(&mut self) -> Option<String> {
        if self.peek()? != b'"' {
            return None;
        }
        self.i += 1;
        let mut s = String::new();
        loop {
            let c = self.peek()?;
            self.i += 1;
            match c {
                b'"' => return Some(s),
                b'\\' => {
                    let e = self.peek()?;
                    self.i += 1;
                    match e {
                        b'"' => s.push('"'),
                        b'\\' => s.push('\\'),
                        b'/' => s.push('/'),
                        b'n' => s.push('\n'),
                        b'r' => s.push('\r'),
                        b't' => s.push('\t'),
                        b'u' => {
                            let mut code = 0u32;
                            for _ in 0..4 {
                                let h = self.peek()?;
                                self.i += 1;
                                code = code * 16 + (h as char).to_digit(16)?;
                            }
                            s.push(char::from_u32(code).unwrap_or('\u{fffd}'));
                        }
                        _ => return None,
                    }
                }
                c => s.push(c as char),
            }
        }
    }

    fn parse_value(&mut self) -> Option<Value> {
        match self.peek()? {
            b'"' => Some(Value::Str(self.parse_string()?)),
            b't' => {
                if self.b[self.i..].starts_with(b"true") {
                    self.i += 4;
                    Some(Value::Bool(true))
                } else {
                    None
                }
            }
            b'f' => {
                if self.b[self.i..].starts_with(b"false") {
                    self.i += 5;
                    Some(Value::Bool(false))
                } else {
                    None
                }
            }
            b'[' => {
                self.i += 1;
                let mut arr = Vec::new();
                self.skip_ws();
                if self.peek()? == b']' {
                    self.i += 1;
                    return Some(Value::Arr(arr));
                }
                loop {
                    self.skip_ws();
                    arr.push(self.parse_string()?);
                    self.skip_ws();
                    match self.peek()? {
                        b',' => {
                            self.i += 1;
                            continue;
                        }
                        b']' => {
                            self.i += 1;
                            break;
                        }
                        _ => return None,
                    }
                }
                Some(Value::Arr(arr))
            }
            _ => {
                // Number: consume a run of numeric characters, keep as text.
                let start = self.i;
                while let Some(c) = self.peek() {
                    if c.is_ascii_digit()
                        || c == b'-'
                        || c == b'+'
                        || c == b'.'
                        || c == b'e'
                        || c == b'E'
                    {
                        self.i += 1;
                    } else {
                        break;
                    }
                }
                if self.i == start {
                    return None;
                }
                Some(Value::Num(
                    std::str::from_utf8(&self.b[start..self.i])
                        .ok()?
                        .to_string(),
                ))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn writer_then_parse_roundtrip() {
        let mut w = ObjectWriter::new();
        w.str_field("op", "fadd.s")
            .u64_field("a", 0xFFFF_FFFF_FFFF_FFFF)
            .bool_field("flush", true)
            .str_array_field("features", &["fp16".to_string(), "neon".to_string()]);
        let line = w.finish();
        let obj = parse_object(&line).expect("parse");
        assert_eq!(obj.get("op").unwrap().as_str(), Some("fadd.s"));
        assert_eq!(obj.get("a").unwrap().as_u64(), Some(0xFFFF_FFFF_FFFF_FFFF));
        assert_eq!(obj.get("flush").unwrap().as_bool(), Some(true));
        assert_eq!(
            obj.get("features").unwrap().as_arr(),
            Some(&["fp16".to_string(), "neon".to_string()][..])
        );
    }

    #[test]
    fn parses_empty_object() {
        let obj = parse_object("{}").expect("parse");
        assert!(obj.is_empty());
    }

    #[test]
    fn rejects_malformed() {
        assert!(parse_object("not json").is_none());
        assert!(parse_object("{\"k\":}").is_none());
        assert!(parse_object("{\"k\" 1}").is_none());
    }

    #[test]
    fn escapes_survive_roundtrip() {
        let mut w = ObjectWriter::new();
        w.str_field("cpu", "weird \"quote\" \\ and\ttab");
        let line = w.finish();
        let obj = parse_object(&line).expect("parse");
        assert_eq!(
            obj.get("cpu").unwrap().as_str(),
            Some("weird \"quote\" \\ and\ttab")
        );
    }
}
