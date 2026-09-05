# Unified-format Swift integration readback

Coordinator read-only review on 2026-09-05 against ADR 0009 and
`config/sync-format.json` (`1bfae11`). Baseline Swift source is `f25eea5`;
the unified owner began changing that source during the review. This is an
integration checklist, not a defect verdict against unfinished work or a test
pass. Ownership is explicitly accepted in `ae740b5`: the same Sync owner
implements C++ and Swift; this reviewer does not edit either product scope.

## Required coherent changes

1. **One live boundary, not only new defaults.** `SharedSyncFormat.swift`,
   `SyncModels.swift`, `CompanionModels.swift` and `SyncBoundary.swift` in
   `spikes/cloudkit` must agree on version 3 and exactly the 13 live classes.
   WIP already adds that central class set, exact-version authorization and
   model defaults. The former general metadata classes are not new wire entities.

2. **All codecs, imports and recovery must agree.** In Mobile Core, baseline
   `DesktopWireSharedTabReadPolicy.swift` still contains v1/v2 read/write paths;
   Bookmark/Capability codecs separately require 2. `CompanionSyncBridge.swift`
   gates live candidate import and recovery at `schemaVersion <= 2`.
   `BookmarkTransportAuthorization.swift` also has a global version guard.
   Change these as one package while retaining category consent and final
   outbound/rehydration checks. Do not delete the consent guard just because
   its former per-version distinction is obsolete. Keep provider domain-merge
   cancellation leases and account/key/recovery isolation.

3. **Strict shared clocks for every class.** Baseline
   `DesktopWirePayloadCodec.version` uses the general integer/UUID helpers and
   `NSNumber.int64Value` for field logical counters, unlike the stricter v3
   Tree/Presence front-door validation. Apply canonical UUID/Windows-us and
   exact UInt32 validation uniformly, rejecting booleans, fractional values,
   unknown/missing field groups and clock/envelope mismatch. Keep positive
   Int64 Bookmark creation metadata independent of the HLC Unix-epoch bound.
   Local explicit field stamping is not permission to synthesize missing clocks
   in received records or retain mixed-version merge/promotion paths.

4. **Fresh snapshot marker without empty fallback.** Baseline
   `CompanionSnapshotCodable.swift` has no format marker and defaults missing
   collections to empty arrays. Require marker 3 before interpreting persisted
   snapshots; an unsupported or marker-less existing file must fail without
   save/reset/overwrite. An actually absent new file may initialize a fresh store.
   Do not reuse old provider checkpoints/inboxes for the fresh acceptance zone.

5. **Every published normal Presence needs its real page.** Baseline
   `SharedTabTarget.validatePresence` exempts web targets from a required link,
   and `DesktopWireTabTargetCodec.validatePresenceTarget` skips web consistency
   checks. `CompanionStore.publishLocalMobileTab` has no TreeNode argument and
   still authors the old field set. Bind the actual page before publication,
   keep local/runtime/Presence identities separate from the logical TreeNode,
   and defer incomplete capture instead of filtering rows into inferred deletes.
   Out-of-order parent delivery remains pending, not a fabricated or empty page.

6. **Functional readiness remains real.** WIP already changes Capability to
   model 3 with read/write lists `[3]`. Capability control metadata must not
   require the shared-tab data writer to be enabled first. Conversely, do not
   advertise automatic native/mobile behavior until its actual binding and
   capture/projection consumers exist. Save/unsave identity, private exclusion,
   no focus/eager load and original-scope authorization remain required.

## Acceptance handoff

Use only the owner's actual canonical `sync_wire_v3.json` bytes once delivered,
referenced by both implementations; no second fixture was created by this review.
Cover the 13 live classes plus rejection of old/unknown versions, malformed clocks
and incompatible stores. Preserve restart/no-echo/consent and representative
native/Mobile journeys. Old mixed-version migration tests may be retired when
their production path is removed, with their old evidence kept historical.

No source files, stores, keys, profiles, app bundles or server data were changed
by this review. No compiler, unit/integration/E2E test or host was started.

## Subsequent common-test review

Execution priority: the user subsequently directed implementation/runnable
candidate and visible E2E first. The optional assertion-review requests below
are deferred until that product flow works; they are not a pre-E2E work package.

The new C++ `sync_unified_serialization_unittest.cc` now reads the actual
canonical file, verifies its hash/26 cases, asserts all 13 entity types and
compares exact reserialized payload bytes. Mobile `project.yml` also includes
the one canonical resource. These are source-level bindings, not execution.

Two focused assertion-isolation issues were sent to the unified owner as
`01a0730e-2e27-7b02-aeba-0efd288e7660`:

- `IncomingMissingOrUnknownFieldsNeverGetSyntheticClocks` removes a required
  field and then inserts the unknown field without restoring the first. Restore
  the required field (or use a fresh valid copy) before the unknown-only case.
- `LogicalNumbersRejectBooleanFractionNegativeAndOverflow` changes only
  `version_logical`. Add isolated `field_versions[*].logical` cases; Boolean
  `false` and fractional `0.5` avoid a spurious pass caused merely by the field
  clock exceeding the record clock after permissive integer conversion.

Status: reported against WIP, no test execution and no fix assumed. Swift test
assertions consuming the new fixture remain separately to be observed.
