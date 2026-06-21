// Copyright (c) Microsoft Corporation.

//! Safe Rust wrapper over the Windows thread pool API.
//!
//! Rust parity with the C++ `m::threadpool` library
//! (`src/libraries/threadpool/`): submit work, timers, and periodic timers to
//! the OS thread pool without dedicating threads. See `DESIGN-NOTES.md` for the
//! design decisions (TP-D1..TP-D3).
//!
//! No implementation yet — this is a scaffold.
