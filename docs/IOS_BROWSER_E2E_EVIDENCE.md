# AhoiBrowser Mobile E2E evidence contract

- Source state: integrated Mobile worktree on 2026-08-30
- Candidate binding: development evidence only; no distribution/TestFlight
  candidate is bound
- Execution recorded for this state: simulator builds/tests plus one signed
  physical-iPhone development installation and bounded visible smoke; no full
  candidate-bound physical journey

This file is the evidence contract and a bounded status summary, not a Mobile
release receipt. The authoritative machine-readable status is
`config/test-registry.json`. Every
`MOB-USER-01` through `MOB-USER-15` entry is release-critical `CU_E2E`
with status `NOT_RUN`. Every `IOS-01` through `IOS-15` entry remains
release-critical `ASSISTED_E2E` with status `NOT_RUN`.

Source presence, previews, screenshots from another revision, an XCTest result,
a simulator launch, an archive, an upload or a processed TestFlight build may
support a later evidence chain, but none changes a registry status by itself.
Evidence must name the repository commit, generated Xcode project inputs,
bundle/build version, Team ID, bundle ID, profile, entitlements, device model,
OS build and retained artifact paths.

## Bounded development evidence from 2026-08-30

| Check | Result | Evidence boundary |
| --- | --- | --- |
| Debug simulator build, iPhone destination | `PASS` | Buildability only; not signing, installation or device behavior. |
| Release simulator build, iPad destination | `PASS` for `arm64` and `x86_64` | Release configuration compiled for the simulator; this is not an archive or distribution signature. |
| `AhoiMobileCoreTests` | 56 executed, 0 failures, 2 skips | Both skips require a real CloudKit entitlement. They remain skips, not passes. |
| `AhoiMobileUITests` | 3 executed, 0 failures | Deterministic simulator fixture, private restore, offline/retry and unsafe-scheme coverage only. |
| CloudKit/security package tests | 36 executed, 0 failures | Offline model, convergence and security contracts; no real CloudKit mutation. |
| Physical iPhone development build | Signed, installed and visibly smoke-tested on an iPhone 16 Pro Max running iOS 26.6 | The first CLI launch was rejected while the device was locked. After unlock, launch through the host's iPhone device-management/synchronization flow visibly proved Ahoi cold start, HTTPS `example.com`, Browser Actions, a new private tab and normal-tab restore with the private tab excluded after targeted Ahoi process termination. This launch path is unrelated to Ahoi CloudKit sync. |
| Physical iPad | Not runnable | The available iPad (6th generation) runs iPadOS 17.7.10 and is incompatible with the app's iOS/iPadOS 26 deployment target. |

None of these bounded results changes a `MOB-USER-*` or `IOS-*` registry
status. The physical smoke proves only the exact visible steps named above; it
does not complete the broader cold-start/navigation, tab lifecycle, private
isolation, VoiceOver, default-browser or CloudKit journeys.

Retained screenshots:

- [`iphone-16-pro-max-01-cold-launch.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-01-cold-launch.png)
- [`iphone-16-pro-max-02-example-https.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-02-example-https.png)
- [`iphone-16-pro-max-03-browser-actions.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-03-browser-actions.png)
- [`iphone-16-pro-max-04-private-tab.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-04-private-tab.png)
- [`iphone-16-pro-max-05-normal-restore.png`](audit-evidence/2026-08-30-ios-device-preflight/iphone-16-pro-max-05-normal-restore.png)

## Source seams awaiting runtime evidence

- system `WebPage`/`WebView` rendering with WebKit-owned networking, TLS,
  dialogs, permissions and website storage;
- persistent normal `WKWebsiteDataStore.default()` plus one shared
  `WKWebsiteDataStore.nonPersistent()` for the running private session;
- `WKDownload` transfers tied to the initiating request/data store, with
  filename sanitization, collision-safe destination, progress, cancellation,
  Quick Look and sharing;
- origin-labelled camera, microphone, motion, JavaScript, file-input and
  external-scheme consent surfaces;
- browser-session persistence that excludes private tabs and serializes writes
  by revision;
- tracked Privacy Manifests declaring no tracking and no tracking domains,
  plus browsing history, search history, other user content and device ID as
  unlinked, non-tracking data used for app functionality, and UserDefaults
  reason `CA92.1`;
- local-first encrypted CloudKit records, a durable fetched-envelope inbox and
  event-driven CKSyncEngine transport without a foreground polling loop.

These bullets describe reviewable source boundaries only.

## Mobile Computer Use journeys

| Journey | Status | Candidate-bound evidence required |
| --- | --- | --- |
| `MOB-USER-01` cold start, URL and HTTPS | `NOT_RUN` | Installed candidate cold launch, URL entry, HTTPS navigation and visible origin on supported iPhone. |
| `MOB-USER-02` search and navigation | `NOT_RUN` | Configured provider, result navigation, Back/Forward/Reload/Stop and no duplicate loads. |
| `MOB-USER-03` tab lifecycle and restore | `NOT_RUN` | Create, reorder, rename, close/undo, terminate and cold-restore normal tabs. |
| `MOB-USER-04` workspace save/move | `NOT_RUN` | Save and move a live page, then reopen it through tree and search. |
| `MOB-USER-05` private separation | `NOT_RUN` | Normal/private cookie and storage probes plus process-death proof that private tabs never enter session, history, search, sync or device tabs. |
| `MOB-USER-06` default-browser callback | `NOT_RUN` | Apple-entitled system default selection and external HTTP(S) callbacks into the exact signed app. |
| `MOB-USER-07` upload/download/share/popup | `NOT_RUN` | Real file provider upload, normal/private authenticated downloads, progress/cancel, Quick Look/share and popup attribution. |
| `MOB-USER-08` permissions/dialogs/external app | `NOT_RUN` | Origin-labelled allow/deny/cancel across main/subframes, JavaScript/file dialogs and external-app confirmation. |
| `MOB-USER-09` rotation and accessibility | `NOT_RUN` | Portrait/landscape, Dynamic Type, VoiceOver, high contrast, Reduce Motion and Reduce Transparency on device. |
| `MOB-USER-10` iPad interaction | `NOT_RUN` | Real iPad sidebar, multitasking, keyboard, pointer, rotation, reorder and workspace gestures. |
| `MOB-USER-11` failure and restore | `NOT_RUN` | Offline/TLS/WebContent failure, background/termination, memory pressure, incomplete download and deterministic recovery. |
| `MOB-USER-12` cross-device tabs | `NOT_RUN` | Final entitled Mac–iPhone/iPad CloudKit and Keychain roundtrip, offline queue, conflict, revoke and private-data exclusion. |
| `MOB-USER-13` unsafe actions | `NOT_RUN` | Reject local/script/credential/unknown schemes and verify labelled confirmation for permitted external schemes. |
| `MOB-USER-14` 1/5/20 tabs | `NOT_RUN` | Normal/private scale, switching, reorder, discard, persistence and absence of phantom tabs. |
| `MOB-USER-15` visual consistency | `NOT_RUN` | iPhone/iPad, normal/private, light/dark, tint/fallback and accessibility appearance matrix. |

## Physical cross-device assisted journeys

| Journey | Status | Required boundary |
| --- | --- | --- |
| `IOS-01` | `NOT_RUN` | Browse workspaces, tree, tabs and history on real iPhone/iPad. |
| `IOS-02` | `NOT_RUN` | Create saved page and folder. |
| `IOS-03` | `NOT_RUN` | Move, rename and delete tree nodes with Mac confirmation. |
| `IOS-04` | `NOT_RUN` | Open link through the selected system default browser. |
| `IOS-05` | `NOT_RUN` | Send link to a selected Mac and workspace. |
| `IOS-06` | `NOT_RUN` | Remotely open one normal Mac tab. |
| `IOS-07` | `NOT_RUN` | Remotely focus one normal Mac tab. |
| `IOS-08` | `NOT_RUN` | Remotely close one normal Mac tab after confirmation. |
| `IOS-09` | `NOT_RUN` | Offline command and TTL behavior. |
| `IOS-10` | `NOT_RUN` | Queued/delivered/executed/failed status progression. |
| `IOS-11` | `NOT_RUN` | Replay rejection across restart. |
| `IOS-12` | `NOT_RUN` | Wrong target and invalid signature rejection. |
| `IOS-13` | `NOT_RUN` | Device approval and revocation. |
| `IOS-14` | `NOT_RUN` | Private tabs remain invisible and uncontrollable. |
| `IOS-15` | `NOT_RUN` | Reject arbitrary schemes, shell commands and bulk actions. |

## External gate mapping

| Gate | State | Closure evidence |
| --- | --- | --- |
| `ios-final-bundle-team-profile` | `blocked-credential` | A development build was signed and installed, but final Team/App/bundle identity plus a distribution certificate/profile are still absent. |
| `ios-managed-default-browser-entitlement` | `blocked-entitlement` | Apple grant, profile attachment and physical default-browser journey. |
| `ios-cloudkit-keychain-capabilities` | `blocked-entitlement` | No real container, entitled Mac counterparty, Keychain keys or operational provisioning/rotation/revocation path exists yet. |
| `ios-physical-device-journeys` | `required-user-assistance` | One bounded iPhone development smoke is retained, but a supported iPad and complete Mobile/IOS journeys on the exact release candidate are still required. |
| `ios-app-store-connect-testflight` | `blocked-external-service` | No distribution certificate, App Store Connect API key, reviewed record/disclosures, archive/export/upload/processing receipt or physical TestFlight install exists. |

This state records the bounded simulator results and physical development
smoke above. It does not claim a complete physical-device journey,
default-browser entitlement, CloudKit roundtrip, distribution archive,
TestFlight installation or Mobile release.
