# iPhone real-device CloudKit Development sync, 29 September 2026

Owner-approved run on the paired iPhone 16 Pro Max "Servusla"
(UDID 00008140-0014294E0A90801C, iOS 27.0 24A437, Developer Mode on,
unlocked, wired). Isolated scope `23855a90-ee61-499e-abed-bfdc52a881d7`
(`../scope.json`): zone `AhoiSyncAcceptance-<id>`, subscription
`AhoiSyncAcceptanceSubscription-<id>`, Keychain account
`payload-key.acceptance-<id>`, Development only. No key, record or zone was
deleted; no manual key copy; no Production; Mac, Mac Keychain, CloudKit
Dashboard and other devices were not touched.

## Build and install

Scheme `AhoiMobile-CloudKitDevelopment`, configuration `CloudKitDevelopment`,
automatic signing for team 248AJ5BN47, own derived data. The scope and
`AHOI_SOURCE_COMMIT` were passed as xcodebuild build settings (nothing
committed); the signing preflight printed "isolated Development scope is
source-bound" and "CloudKitDevelopment settings match the exact public
identity and entitlement contract". Built Info.plist and signed entitlements
were checked: zone/subscription/account above, `aps-environment=development`,
`icloud-container-environment=Development`, Keychain group
`248AJ5BN47.app.ahoibrowser.sync`.

```
xcodebuild build-for-testing -project AhoiMobile.xcodeproj \
  -scheme AhoiMobile-CloudKitDevelopment -configuration CloudKitDevelopment \
  -destination id=00008140-0014294E0A90801C -derivedDataPath <worktree>/.work/realsync/dd \
  -allowProvisioningUpdates AHOI_SOURCE_COMMIT=<HEAD> \
  AHOI_SYNC_ACCEPTANCE_SCOPE_ID=<id> AHOI_CLOUDKIT_ZONE_NAME=AhoiSyncAcceptance-<id> \
  AHOI_CLOUDKIT_SUBSCRIPTION_ID=AhoiSyncAcceptanceSubscription-<id> \
  AHOI_SYNC_KEYCHAIN_ACCOUNT=payload-key.acceptance-<id> DEVELOPMENT_TEAM=248AJ5BN47
xcrun devicectl device install app --device 00008140-0014294E0A90801C AhoiMobile.app
TEST_RUNNER_AHOI_REAL_DEVICE_CLOUDKIT_SYNC=1 TEST_RUNNER_AHOI_REAL_DEVICE_SYNC_TAB_URL=<url> \
  xcodebuild test-without-building -xctestrun <...>.xctestrun \
  -destination id=00008140-0014294E0A90801C \
  -only-testing:AhoiMobileUITests/MobileRealDeviceCloudKitSyncUITests
```

Each run used a new build of the then-current commit; all builds and
installs ended EXIT0. The final passing candidate is source
`d065a74400f67293a54d949b8b44aabc53802866` (hashes in
`candidate-d065a744.sha256`). The source changes between runs were only in
the UI test, the app sources were identical.

## Runs

- **Run 1** (source `85574661`, fresh scoped stores, first opt-in):
  `run1-*`. Sync toggle was `0`, tapped to `1`. Status went from
  "Nur lokal" / "Sync-Schlüssel sind deaktiviert" to **"Bereit" /
  "Verschlüsselung bereit"** within seconds (screenshots 03 → 05). The
  test then failed at address entry (the whole-URL keystroke injection lost
  characters, "ht" became "h"), so no tab was created in this run.
- Run 2 (`62ae725e`) and run 3 (`f76146de`) failed at test-harness steps
  (localized example.com body; address control reported unhittable over
  the restored page). They are not kept as evidence.
- **Run 4** (source `d065a744`, PASSED, 121.7 s): `run4-*`. Sync was
  already on from run 1. Launch state "Bereit" / "Verschlüsselung bereit" /
  detail "Synchronisiert". The test typed and opened
  **`https://example.com/?ahoi-sync-ios-20260929T105445Z`** (visible
  address matched exactly, page loaded), sent the app to Home and back
  (normal publish path), then tapped "Jetzt synchronisieren". Two seconds
  later and still after the 3 s minimum observation: **Status "Bereit",
  Verschlüsselung "Verschlüsselung bereit", detail "Synchronisiert"**, no
  setup-issue footer. Devices lists "iPhone · Dieses Gerät · Online".
  Transitions: `run4-sync-status-transitions.txt`; visible Settings
  texts: `run4-settings-visible-texts.txt`; final screenshot
  `run4-07-after-sync-now.png`.

## Boundaries

The app sets "Synchronisiert" only after a bounded pass with no pending
outbound records ("Änderungen sind noch ausstehend …" otherwise). That is
the phone's own upload signal. The pre-tap status was already
"Synchronisiert" and no "Wird synchronisiert" was sampled, so a separate
readback of the specific tab record is still open. Nothing on the phone
proves that the Mac received it. Run 1 reached encryption-ready within
seconds. If no other device had used this scope before, the iPhone created
its bootstrap claim and synchronizable iCloud-Keychain key. A Mac peer on
the same scope then has to receive that key through iCloud Keychain before
it can decrypt the tab. This run does not show which device created the key.
