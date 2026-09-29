# Build17 visible Workspace-tab journey

Candidate: `7b706a73f98802fe31b206731b7e88c1da6b6c59`, DebugLocal 0.1 (17),
iPhone17Pro/iOS26.5. The installed app was independently verified against
`../../build/mobile-shared-intents-7b706a7-20260908/candidate.json`, including
source/plist, signature, app tree and generated Xcode project.

The explicit user Simulator handoff is recorded at 2026-09-08 07:08 UTC in
`docs/ACTIVE_SYNC_COORDINATION.md`. A fresh device was created for this run:
`F8253C50-E423-4424-8EE3-5F152C593A31` / `Ahoi Unified Sync Build17 20260908`.
No existing device/profile was used for the journey. Capacity was checked before
boot. UI actions used the connected Simulator app through Computer Use; device
install/boot/shutdown and scoped data readback used the exact simulator UDID.

## Bounded visible result: PASS for Navigate -> Save -> Restart

1. Launch via the installed app icon: normal empty browser, one placeholder tab.
2. Enter `https://example.com/?ahoi-unified-17` through the real address field.
   The Example Domain page visibly rendered; the tab acquired canonical Inbox.
3. Tab list showed `Inbox · Temporär`, one selected Example Domain row.
4. Browser actions -> expand native sheet -> In Workspace sichern -> Inbox.
5. Tab list showed `Inbox · Gespeichert`, same selected runtime row, no duplicate.
6. Home/background, then shutdown+boot of ONLY the fresh test device. Launch
   via native app search. The same webpage loaded; exactly one saved tab remained
   in Inbox, with the same accessibility row ID.

`navigation.png` and `saved-tab.png` were captured using Simulator's Save Screen
control and moved from its temporary Desktop destination into this directory.
The restart screenshot/AX was observed in the UI tool transcript; its exact
owned-window row evidence is transcribed in `restart-ui.ax.txt`. No later foreign
window output is used as Ahoi evidence.

The two raw browser/domain JSON snapshots are scoped to this fresh, synthetic
Example Domain fixture. Readback after the visible journey confirmed:

| Identity | Before and after restart |
| --- | --- |
| Runtime | `391646FB-23D6-4C98-AB7F-C214BF00CB17` |
| Presence | `E4A95271-FF3A-4371-9883-525FED893F81` |
| TreeNode | `18126D0F-F484-5735-BEFC-30C92B8B036B` |
| Inbox | `83699047-EDF8-580D-948D-9C37ACC35CB6` |

All three tab identities are distinct and stable. Both snapshots contain exactly
one saved runtime tab. Domain format is3; Presence links to the same Page and is
pinned. Entire Workspace and TreeNode values/field clocks are identical by structured JSON comparison
before/after restart: passive restoration did not re-author these records.
No pending mobile mutations/receipts remained after the explicit background flush.

## Limits / tool incident

- Unsave was NOT visibly accepted. The CUA API provides no sustained touch;
  right-click and stationary drag selected the row rather than opening its
  SwiftUI long-press context menu. No internal model mutation was substituted
  for the missing user gesture, and no product defect/pass is inferred from it.
- After the Ahoi restart result was already visible, the final attempted
  Save Screen/Home call unexpectedly reported window `MindBodyCompass Pilot QA`.
  The two actions' window attribution is unproven; a foreign screenshot/Home
  effect cannot be excluded. All UI actions stopped immediately. No foreign
  app content or file was read or copied into this evidence.
- The exact OWN simulator was shut down and its `Shutdown` state verified.
  It remains as an isolated stopped test store for follow-up; no foreign device
  was addressed by CLI. UI handback: `01a0801b-0086-71f0-97f5-46f712d925ad`.
  Coordinator notified of the possible cross-window effect:
  `01a0801b-00da-7682-abdb-944ab484f1d4`.

## Minimal checks AFTER the visible journey

`swift test --package-path spikes/cloudkit --jobs 2 --disable-index-store
--filter AhoiCloudKitSpikeTests.SyncBoundaryTests` ended EXIT0, **4 XCTest cases
executed / 0 failures**. The trailing Swift Testing runner discovered zero cases;
it is not the source of the four-case pass. Log: `sync-boundary-tests.log`.
Only the four existing boundary methods were updated from obsolete V1/V2/alias
expectations to the current format3 contract, then run. No product bytes changed.
This is a native CLI/shared-source test, not a My-Mac app host, real CloudKit,
cryptographic roundtrip, Chromium, Unsave UI or full mobile regression pass.

This journey covers a normal Workspace tab, NOT Chromium Bookmarks or the
historical Library Bookmark CRUD flow. Genuine Mac/iOS cross-client convergence,
Unsave UI, deferred/replay fault timing, full browser setup/extension restoration
and the other goal gates remain open.
