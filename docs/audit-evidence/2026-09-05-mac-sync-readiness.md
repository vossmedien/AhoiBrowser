# Mac Sync readiness — installed candidate readback

Date: 2026-09-05. Installed Chromium candidate:
`3d413efb5b6f196403e92f51631c346c9c55b2e5`, Chromium `152.0.7977.65`.
Receipt: `artifacts/install/ahoi-dev-3d413ef-20260905T074543Z.json`.

| Boundary | Current evidence |
| --- | --- |
| Build / install | Guarded build and atomic install both exited 0; post-install verification true |
| Code signing | Apple Development: Christian Voss (2265UJB5KF); actual TeamIdentifier `248AJ5BN47` |
| Runtime CloudKit configuration | No `AHOI_CLOUDKIT_CONTAINER_ID` or `AHOI_SYNC_KEYCHAIN_ACCESS_GROUP` in installed Info.plist |
| CloudKit permissions / profile | No CloudKit entitlement readback; no `Contents/embedded.provisionprofile` |
| Provider construction | `FromMainBundle()` returns nullopt because the container is absent; backend does not construct the provider |
| Shared payload-key bootstrap | Not verified; missing group/config is a separate boundary, not the predicate that returns nullopt |
| Native Mac CloudKit candidate | No separately prepared and verified candidate in this Desktop wave |
| C++ sync unit executable | GN target exists, but `out/AhoiDev/ahoi_sync_unittests` is absent |

The development signature is not CloudKit transport or Production evidence.
Provider-free local persistence remains usable; this report did not enable Sync,
query a payload secret, change entitlements or mutate Apple/CloudKit state.

Existing implementation preparation is documented in `docs/SYNC.md` and
`config/macos-entitlements.json`: distinct `cloudkit-development` and
`cloudkit-production` profiles, public container/group identifiers and the
`prepare-macos-cloudkit` / `verify-macos-cloudkit` release-tool commands.
Their existence is not a prepared/signed runtime candidate. The old Apple-portal
snapshot in SYNC.md was not refreshed here and must not be treated as current
proof that an Ahoi container/profile does not exist.

Next coordinated package must include `ahoi_sync_unittests` and a verified Mac
CloudKit signing/configuration path with a concrete compatible Mac provisioning
profile, candidate-bound receipts, payload-key bootstrap and Mac–Mobile roundtrip.
No extra build was started for this coordination request. Existing iPhone-hosted
two-store/bridge Development or simulated tests do not prove Chromium execution,
cross-device Keychain bootstrap or Production transport. Use Mobile's checkpoint
for its exact run/device/server evidence; do not relabel historical Workspace
Library tests as Chromium bookmark or shared-normal-tab acceptance.

## Next combined build / focused C++ test handoff

Build the actual `ahoi_sync_unittests` executable with the next suitable guarded
incremental package. A `runtime_deps` file alone is not a built/testable candidate.
Do not restart the completed `3d413ef` build or give a test-execution handoff
before a binary, exact source/build receipt and executable hash exist.

These requested suites are present in the current source and must be retained
in the focused run (verify discovery/nonzero matches on the actual executable):

- `SyncWireV2Test.*`
- `SyncPayloadCryptorTest.*`
- `SyncStoreV3Test.*`
- `SyncPumpTest.*`
- `DeviceTabsServiceTest.*`
- `TabTreeSyncAdapterTest.*`
- `RemoteCommandSecurityTest.*`

The new Mobile scenario has separate repositories, bridges and CKSyncEngines
against a fresh real Development zone for bidirectional workspace/saved-page/tab
changes, offline field merge, tombstones and privacy. Its Mac identity remains
simulated; test design must not be reported as completed execution.

Shared C++/Swift Workspace and SavedPage transcripts remain a specific missing
gate. Existing domain tests and Tab/AES/Ed25519 golden contracts do not close it.
After the shared field/version handoff, both actual language implementations
must consume one version-bound fixture for wire/state expectations, including
both directions, offline field edits and delayed records after deletion. Do not
create independent duplicated goldens or alter shared schema/clock semantics
while the bookmark owner holds those files. Mobile keeps its Swift/test ownership;
Desktop owns C++ build integration and the candidate-bound execution handoff.

The isolated desktop UI slot was subsequently returned by the bookmark owner
after the same native-pipe failure; see the current Desktop checkpoint. A Mobile
test host running on My Mac with the same bundle identifier still requires an
explicit runtime handoff before launch; it must not collide with the installed
Chromium candidate.
