# PLANS — wordy

The active plan that governs `wordy` lives with the isolation strategy it proves,
in the `windows-win32-shim` component. `wordy` itself stays shim-unaware (see
`DESIGN-NOTES.md`, WD-D1); this file just points at the governing checklist.

| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|
| [../windows-win32-shim/CHECKLIST.md](../windows-win32-shim/CHECKLIST.md) (MW13–MW15) | in progress | Build `wordy` as a shim-unaware HWC proof app: sync dictionary service (MW13), async on the Windows thread pool (MW14), and the isolation proof (MW15). | [DESIGN-NOTES.md](DESIGN-NOTES.md), [../windows-win32-shim/DESIGN-NOTES.md](../windows-win32-shim/DESIGN-NOTES.md) (SHIM-D19) |
