# Provider upload ordering — source handoff, not product acceptance

The real55ab/bba Native pair demonstrated working History transport but no new
IANA shared Page/Presence in A's Common store or B. Read-only diagnosis found
A still held B's empty capability version13433754955710720. B held/acknowledged
13433754955745728 with `shared-normal-tabs-v3`. B's durable Inbox first received
the feature-bearing version at06:35:58UTC, then the empty version at06:54:38;
A's Inbox contained only the empty version at06:50:32. No current server query,
database mutation, key/profile action or new UI run was performed.

The existing admission gate correctly blocks A's shared-tab writer when B's
feature declaration is empty. This is not evidence for a Native capture bug.
Source and retained logs support the provider's in-/across-batch backward-write
path: ReadOutbox returns multiple originals for one entity, Upload retained
only the last original mutation ID, and the next page could send the older
remaining payload using the just-saved newer CKRecord's ChangeTag. The saved
ciphertext/ID checks and exact-version acknowledgement receipts do not support
the alternative claim that the newer mutation was ACKed for older saved bytes.
Runtime evidence is under `artifacts/e2e/native-peer-55abcf7-20260913/`.

## Implemented, within the two-file permission

- `cloudkit_sync_provider_mac_consent.mm` retains every original SyncChange
  (ID/version/payload), validates groups and selects by the existing field merge.
  Equivalent field states prefer the highest supplied envelope; no provider
  clock is created. All original per-setting leases are combined and held.
- `cloudkit_sync_provider_mac_internal.h` uses MergeRecordFields dominance,
  including immutable fields, field-clock conflicts, terminal command state and
  the absorbing archive tombstone, before ACKing each covered original.
- Known server data is compared before encoding with its ChangeTag and again
  when CKSyncEngine requests the pending record. Uncovered/conflicting data
  stays queued; actual server or unchanged local original inputs are staged
  for the existing durable Inbox/domain merge, with original authority checks.
- An incomparable already-staged input is drained first, never overwritten.
  Actual encrypted bookmark server records remain retained across opt-out.
  The SDK pending request is removed only after proven server coverage; the
  durable outbox is never cleared/reset. Cached coverage and unresolved merge
  outcomes have numeric non-secret diagnostic stages.

These changes are source-only: no build, test, CloudKit operation or ACK pass.

## Pagination completion — implemented19 September, not runtime accepted

The original `SyncStore::ReadOutbox` ordered only by creation time. Its actual
default accepted-row limit is100. A legitimate new domain convergence could
remain behind a page of its own unacknowledgeable originals, preventing the
covering record from ever reaching this provider.

Root resumed the stopped worker's exact handoff and owns this one-file seam.
The query now groups by entity type/ID and preserves the oldest queued entity's
priority, while yielding its existing versions newest-first. Tied envelopes
prefer the latest queued convergence. All original rows remain present; the
category filter, per-setting authorization callback and accepted-row limit are
unchanged. The provider still proves field dominance independently of query
order. No schema, wire, engine, public API or clock authority was added.

Core sourceeffe985 plus this pagination completion and Nativea47 form the next
single combined source candidate. Product build, visible peer-arrival and the
necessary focused regressions remain open; no new code is retroactively covered
by the55ab or DebugLocal25 evidence.

Do not build the old a47-only script or this intermediate Core-only revision.
The next product build combines Nativea47 and the complete provider fix,
with a fresh total-capacity check. See the current Desktop checkpoint for its
actual source/handle; the13September disk snapshots are not current gates.
