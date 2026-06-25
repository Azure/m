# windows-file-io — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
The driving milestone (`MW18`) lives in
[`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md); this
file scopes the component-local work.

---

## FIO1 — Native async overlapped file I/O (MW18-1)

- [x] **FIO1-1** `windows-file-io-sys` unsafe leaf: RAII overlapped `FileHandle`
      (exposes the raw handle), `open`/`file_size`/`set_end_of_file`, and
      `OverlappedOp` issuing one overlapped `ReadFile`/`WriteFile` with the
      `Issue::{Pending,Eof,Failed}` outcome (D-FIO-1/2/3/6).
- [x] **FIO1-2** Safe `windows-file-io` crate: `File` (`open`/`create`/
      `open_read_write`, async `read_at`/`write_at`/`write_all_at`/`read_to_end`,
      `size`/`set_len`) over the `windows-threadpool` IOCP reactor; owned
      `FileError` (D-FIO-4/5). 13 unit tests + a stress integration test
      (`tests/stress.rs`: 256 KiB chunked round-trip, 300-file round-trip,
      truncating rewrite).
