# Additional sync assurance — 2026-09-05

The user requested real or simulated sync verification beyond the completed
internal-beta goal. These changes add tests only. There is no change to Mobile
product code, the shared schema, bookmark sources, Desktop checkout or installed
Desktop app.

This describes the test-only wave through `8f98cc1`. The subsequent user request
for shared normal tabs is a new implementation wave (ADR 0007), with changed
Mobile product source and its own pending acceptance. None of this report's
older passes accepts that newer behavior.

## Executed: two independent domain replicas

`CompanionConvergenceTests.testTwoIndependentRepositoriesConvergeThroughSerializedEncryptedRelay`
passed **1/1, zero failures/skips**, in 0.155 seconds on iPhone 17 Pro Max /
iOS 26.5 Simulator. Source: `8f98cc18d4e46b73ea938864a9a586a9bd35e4e9`,
DebugLocal test candidate `0.1 (11)`.

Both replicas use separate file-backed `LocalFirstRepository` instances and
separate encrypted record stores. They execute the real `CompanionSyncBridge`,
`DesktopWirePayloadCodec`, `AppleCloudKitRecordCodec`, CryptoKit AES-GCM sealer
and domain field merge. Only transport delivery uses the existing explicit
DEBUG test transport, with a known fixture key. No Apple account, user key or
real CloudKit container is constructed by this simulation.

The assertions cover:

- Workspace, saved page and normal device tab from logical Mac to iPhone.
- A page edit and the other device's normal tab back to the first replica.
- Independent offline title/URL edits converging without losing either value.
- A genuinely stale encrypted live record delivered after deletion; the page
  remains tombstoned and absent from the visible tree.
- Repeated delivery without duplicate device tabs.
- An incognito record rejected before the encrypted store/relay.
- New repository instances reopening the persisted nodes and tabs exactly.

This is a simulation of the Mobile domain pipeline, not execution of Chromium's
C++ client, real network delivery, Production CloudKit or Keychain replication.
The earlier 213 passing Core cases, shared Swift golden-vector tests and the
36/36 CloudKit package result remain applicable: product/package source has not
changed since those proofs.

## Built and signed: real CloudKit domain exercise

`AhoiMobileCloudKitE2ETests.testRealContainerTwoLogicalDevicesMergePagesTabsAndDeletion`
compiled and signed successfully at source
`9395a9cf2851e2785eeba78afff4902c5d8f3a62`, CloudKitDevelopment `0.1 (11)`.
Strict deep signature verification passed. The intended run uses the existing
signed-host, exact-container, fresh-zone and authenticated-cleanup guards.

Two independent providers/repositories would exchange Workspace/Folder/Page,
Device/Session/Tab records through the real Development server, including
bidirectional changes, offline field merge, a stale pending write after delete,
server-field privacy and a private-record server-negativity check. The two
logical identities still execute Swift/Mobile code and share a run-local key;
this does not substitute for a native Chromium or cross-device Keychain run.

**Execution is pending**, not passed. At 08:38 CEST the iPhone still required
its passcode; iPhone Mirroring subsequently reported the device not found. The
user was asked to unlock it. Build-for-testing performed no CloudKit mutation.
Prepared token `9C8AAE2C-E9C0-435F-B48F-7141E60FD038` remains unused. Retire it
after any actual mutation attempt.

Ready runner:
`/private/tmp/ahoi-mobile-21de889-cloudkit-real-e2e-derived/Build/Products/AhoiMobile-CloudKitE2E_iphoneos26.5-arm64.xctestrun`.
Execute only the method above on `Servusla` after the normal CPU/device checks.

Xcode also advertises My Mac (Designed for iPad/iPhone). Apple documents native
iOS XCTest execution on Apple silicon in its
[iPad and iPhone apps on Apple silicon Macs session](https://developer.apple.com/videos/play/wwdc2020/10114/).
That alternative was not launched: the host shares the installed native
browser's bundle ID, so a short runtime handoff was requested from the Desktop
owner before potentially interfering with its UI acceptance.

## Native Mac / Production boundary

At the time of this diagnostic, native Mac candidate `1f5f22f` lacked
`AHOI_CLOUDKIT_CONTAINER_ID` and the dedicated sync Keychain configuration.
`CloudKitSyncConfigurationMac::FromMainBundle` therefore supplies no transport
configuration. Fixing/configuring that candidate belongs to the Desktop owner.
No `ahoi_sync_unittests` executable was found then; the owner was asked to
include it in the next suitable incremental build. Existing C++/Swift golden
contracts cover Remote Tab bytes, AES-GCM and Ed25519, but a current C++ run
and a bidirectional Workspace/SavedPage transcript remain unverified.
The Desktop owner has since built newer candidates; recheck its current
installed receipt/configuration before reusing that historical diagnosis.

Consequently this report closes the additional Mobile **simulation** check. It
does not mark the full production cross-device sync requirement complete.

## Retained artifacts

Raw artifacts are retained below the canonical project in the intentionally
Git-ignored `artifacts/e2e/mobile-sync-assurance-8f98cc1/`. Hashes use
`tools/mobile_evidence_artifacts.py:sha256_path` (domain-prefixed file/tree hash).

| Artifact | Result | SHA-256 |
| --- | --- | --- |
| `ahoi-mobile-domain-relay-8f98cc1.xcresult` | Simulation 1/1 passed | `f76db3f37822f78b0620ca582a9d4d469667aa04e5c148a23967dcb669279dfc` |
| `ahoi-mobile-domain-relay-8f98cc1.log` | Test succeeded | `b4c31153847b8d09fa4a17c81f491914aee560a6e6748724166eb8613d903b0e` |
| `ahoi-mobile-domain-9395a9c-build.xcresult` | Build only | `220f1c7bcc5b3ac867f596eb41419797126a9e0f3a133bc4dbc6b06e75442e87` |
| `ahoi-mobile-domain-9395a9c-build.log` | Test build succeeded | `a27ea2a52eefea6c893ab486072c8fd403c3439cae7e9975a4f2b549158deb57` |
