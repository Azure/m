// Copyright (c) Microsoft Corporation.
//
// MW18-1: integration stress test for `windows-file-io`.
//
// Drives the async overlapped read/write path at integration scale through the
// real Windows thread pool: a large single-file round-trip written in many
// small chunks (exercising the offset loop and the completion reactor under
// load) and a many-file round-trip. Each operation genuinely goes through
// `StartThreadpoolIo` + an overlapped `ReadFile`/`WriteFile` + an awaited
// completion.

#![cfg(windows)]

use std::path::{Path, PathBuf};

use windows_file_io::File;
use windows_threadpool_executor::block_on;

/// A scratch directory removed on drop.
struct ScratchDir {
    path: PathBuf,
}

impl ScratchDir {
    fn new(tag: &str) -> Self {
        let mut path = std::env::temp_dir();
        path.push(format!(
            "wfio_stress_{tag}_{}_{:?}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&path).expect("create scratch dir");
        Self { path }
    }
    fn child(&self, name: &str) -> PathBuf {
        self.path.join(name)
    }
}

impl Drop for ScratchDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

/// Deterministic payload of `len` bytes seeded by `seed`.
fn payload(seed: u64, len: usize) -> Vec<u8> {
    let mut state = seed.wrapping_mul(0x9E37_79B9_7F4A_7C15).wrapping_add(1);
    (0..len)
        .map(|_| {
            // xorshift-ish, deterministic and dependency-free.
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            (state & 0xFF) as u8
        })
        .collect()
}

fn write_in_chunks(path: &Path, data: &[u8], chunk: usize) {
    let mut f = File::create(path).expect("create");
    block_on(async {
        let mut offset = 0u64;
        for slice in data.chunks(chunk) {
            f.write_all_at(offset, slice).await.expect("write chunk");
            offset += slice.len() as u64;
        }
    });
}

#[test]
fn large_single_file_round_trips_in_chunks() {
    let dir = ScratchDir::new("large");
    let path = dir.child("blob.bin");

    // 256 KiB written in 61-byte chunks (a prime, so the last chunk is partial),
    // then read back in 53-byte chunks.
    let data = payload(0xC0FF_EE00, 256 * 1024);
    write_in_chunks(&path, &data, 61);

    let mut f = File::open(&path).expect("open");
    assert_eq!(f.size().expect("size"), data.len() as u64);

    let mut got = Vec::with_capacity(data.len());
    block_on(async {
        let mut offset = 0u64;
        loop {
            let mut buf = [0u8; 53];
            let n = f.read_at(offset, &mut buf).await.expect("read");
            if n == 0 {
                break;
            }
            got.extend_from_slice(&buf[..n]);
            offset += n as u64;
        }
    });
    assert_eq!(got, data);
}

#[test]
fn many_files_round_trip() {
    let dir = ScratchDir::new("many");
    const COUNT: u64 = 300;

    // Write COUNT files, each with a distinct deterministic payload.
    for i in 0..COUNT {
        let path = dir.child(&format!("word_{i:04}.dat"));
        let data = payload(i, 64 + (i as usize % 128));
        let mut f = File::create(&path).expect("create");
        block_on(f.write_all_at(0, &data)).expect("write");
    }

    // Read each back and verify it matches the regenerated payload.
    for i in 0..COUNT {
        let path = dir.child(&format!("word_{i:04}.dat"));
        let expected = payload(i, 64 + (i as usize % 128));
        let mut f = File::open(&path).expect("open");
        let got = block_on(f.read_to_end()).expect("read");
        assert_eq!(got, expected, "file {i} mismatch");
    }
}

#[test]
fn rewrite_shrinks_via_create_always() {
    let dir = ScratchDir::new("rewrite");
    let path = dir.child("rw.bin");

    // Big, then small: CREATE_ALWAYS must truncate so no stale tail remains.
    let big = payload(1, 4096);
    write_in_chunks(&path, &big, 128);

    let small = payload(2, 7);
    {
        let mut f = File::create(&path).expect("recreate");
        block_on(f.write_all_at(0, &small)).expect("write small");
    }

    let mut f = File::open(&path).expect("open");
    let got = block_on(f.read_to_end()).expect("read");
    assert_eq!(got, small);
}
