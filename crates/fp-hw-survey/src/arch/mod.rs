// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Architecture dispatch. Exactly one backend is compiled in per target; all
//! expose the same `arch_tag` / `supports` / `eval` surface so the rest of the
//! tool is architecture-agnostic.

#[cfg(target_arch = "aarch64")]
mod aarch64;
#[cfg(not(any(target_arch = "aarch64", target_arch = "x86_64")))]
mod unsupported;
#[cfg(target_arch = "x86_64")]
mod x86_64;

#[cfg(target_arch = "aarch64")]
pub use aarch64::{arch_tag, eval, supports};
#[cfg(not(any(target_arch = "aarch64", target_arch = "x86_64")))]
pub use unsupported::{arch_tag, eval, supports};
#[cfg(target_arch = "x86_64")]
pub use x86_64::{arch_tag, eval, supports};
