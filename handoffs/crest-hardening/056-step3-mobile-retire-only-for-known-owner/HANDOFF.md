# 056 – ADR 0011 step 3, mobile: retire a separated Workspace only for its known owner

Status: ready
Owner lane: mobile (apply, run `SeparatedWorkspaceSyncTests` in the simulator)
Base: HEAD `1716fea` (`git apply --check` passes). Implements review 026 by
the crest-hardening lane; not compiled or run by the lane (no builds).

## State of step 3 on mobile

Already integrated and reviewed (024): per-Workspace zones
`AhoiBrowserSyncV3-ws-<uuid lowercase>` (`SyncNamespace.swift`, byte-equal to
`sync_namespace.h`, pinned by `testConstantsMatchTheDesktopHeader`), discovery
through `allRecordZones`, opt-in per Workspace, `WKWebsiteDataStore(forIdentifier:)`
keyed by the Workspace UUID, idempotent store removal (`allDataStoreIdentifiers`
guard) with pending retry, tombstone retirement, key-loss pause and account
binding (`fd6c3b2`, `9347aae`, `57b6ee1`). The owner's three conditions
(identical zone names, idempotent tombstone handling, stable
`forIdentifier` per Workspace id) are met by that code. The code comments call
this "ADR 0011 step 4"; the lane calls it step 3.

The only open finding was 026: two paths still deleted a separated
Workspace's logins without a confirmed matching account.

## Change (`SeparatedWorkspaceSync.swift`)

In `applyDiscoveredZoneNames`, a known Workspace whose zone is missing from
a successful listing is:
- **retired** (`.zoneRemoved`) only when the listing's account and the
  Workspace's recorded owner are both known and equal;
- **left unchanged** when the current account is unknown (the account
  lookup failed or there is no account), which is 026 (1);
- **paused** (`.otherAccount`) when the owner is unknown (records from before
  `57b6ee1`), which is 026 (2), or differs. A later listing that contains
  the zone binds the owner and resumes it, as before.

A tombstone in the Workspace's own zone still retires it through
`syncEnabledWorkspaces`, unchanged.

## Tests (`SeparatedWorkspaceSyncTests`)

- New `testUnknownCurrentAccountNeverRetires`: the listing succeeds, the
  account lookup throws, nothing is retired.
- New `testOwnerlessWorkspaceIsPausedNotRetired`: an ownerless record is
  paused by a known account's listing without its zone, then bound and
  resumed.
- `testListingFailureNeverRetiresAndMissingZoneDoes` and
  `testFailedDataStoreRemovalStaysPendingAndRetries` now give their
  listings a known account (`"_me"`); they relied on the account-blind
  retirement that this change removes.
- `FakeZoneLister.accountError` added.

Run:
`xcodebuild -project apps/AhoiMobile/AhoiMobile.xcodeproj -scheme AhoiMobile
-configuration DebugLocal -destination 'platform=iOS Simulator,id=<UDID>'
-parallel-testing-enabled NO -only-testing:AhoiMobileCoreTests/SeparatedWorkspaceSyncTests test`
(expected: 20/0). WS-ISO-21/22 (real CloudKit) stay owner-gated as before.
