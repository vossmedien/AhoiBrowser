# AhoiBrowser Mobile E2E evidence contract

- Source state: current Mobile worktree on 2026-08-29
- Candidate binding: none
- Execution in this documentation update: no build or test run

This file is an evidence plan, not an execution receipt. The authoritative
machine-readable status is `config/test-registry.json`. Every
`MOB-USER-01` through `MOB-USER-15` entry is release-critical `CU_E2E`
with status `NOT_RUN`. Every `IOS-01` through `IOS-15` entry remains
release-critical `ASSISTED_E2E` with status `NOT_RUN`.

Source presence, previews, screenshots from another revision, an XCTest result,
a simulator launch, an archive, an upload or a processed TestFlight build may
support a later evidence chain, but none changes a registry status by itself.
Evidence must name the repository commit, generated Xcode project inputs,
bundle/build version, Team ID, bundle ID, profile, entitlements, device model,
OS build and retained artifact paths.

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
- tracked Privacy Manifests declaring no tracking/no collected data and
  UserDefaults reason `CA92.1`;
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
| `ios-final-bundle-team-profile` | `blocked-credential` | Final Team/App/bundle identity plus matching development and distribution signing profiles. |
| `ios-managed-default-browser-entitlement` | `blocked-entitlement` | Apple grant, profile attachment and physical default-browser journey. |
| `ios-cloudkit-keychain-capabilities` | `blocked-entitlement` | Final container/environment, entitlements, Keychain groups/keys and key-lifecycle proof. |
| `ios-physical-device-journeys` | `required-user-assistance` | Exact signed candidate installed and all Mobile/IOS journeys executed on real iPhone and iPad. |
| `ios-app-store-connect-testflight` | `blocked-external-service` | Reviewed App Store record/disclosures, archive/export/upload/processing receipts and physical TestFlight install. |

The source state does not claim a simulator result, device result, CloudKit
roundtrip, archive, TestFlight installation or Mobile release.
