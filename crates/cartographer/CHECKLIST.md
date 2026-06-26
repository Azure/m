# CHECKLIST — `cartographer`

The observed-environment descriptor (D-CART-4, milestones **EM-A..EM-E**) is **complete**
and archived in [COMPLETED-CHECKLIST.md](COMPLETED-CHECKLIST.md). Design: `D-CART-4` in
[DESIGN-NOTES.md](DESIGN-NOTES.md).

_No active work._

## Deferred (intentional, not gaps; tracked elsewhere)

- Richer inbound-caller attribution may want additional shim capture (e.g. an
  authority/identity field); the first pass defaults to one client role per seam.
  Recorded here so the absence is deliberate, per D-CART-4. **Blocked on PII-A
  (tokenized identity):** any caller-distinguishing by principal identity must consume tokens,
  never raw identities — see [`../../CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md).
- The recast/plan document (rebinding role → provider) and any replay / fault
  executor are downstream of cartographer and out of scope for these milestones.
- PII tokenization of identities and likely-PII request data (api-journal **D-AJ-4**) is
  deferred and tracked in the root [`CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md);
  milestone PII-D covers cartographer not re-leaking identities into specs or the descriptor.
