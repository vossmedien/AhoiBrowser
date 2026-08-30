# AhoiBrowser Mobile UI and sync development evidence

This directory retains still images from focused iOS Simulator runs on
2026-08-30. It is a bounded development record for the shared working tree,
not a release receipt.

## Source and candidate boundary

- Repository: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`
- Shared branch: `codex/desktop-core-feature-wave-20260830`
- Base `HEAD` when these retained stills were curated:
  `f4cf038580c3622772960532eb0a167ec9156f6f`
- Source state of the stills: dirty shared development tree containing
  coordinated Mobile and Desktop work. The Mobile product sources were later
  committed as `88e9b12e629aaad4e69a590f530754b983d38774`, but these older images are
  intentionally not relabelled as commit-bound evidence.
- iPhone destination: `AhoiBrowser E2E iPhone`, iOS 26.5 Simulator,
  `15C1EB97-A65C-4D93-842D-AB889339BE8D`.
- iPad destination: `AhoiBrowser E2E iPad`, iOS 26.5 Simulator,
  `712F0CD5-D7F9-4B4B-8254-CFBCA4D138F9`.

These images do not change any `MOB-USER-*` or `IOS-*` registry status. All
30 release-critical journeys remain `NOT_RUN` in
`config/test-registry.json`.

## Test ordering

Visible, exact-build E2E is run first whenever the requested behavior can be
exercised safely. Build, signing, installation and fixture preparation may
precede that interaction because they are prerequisites, not substitutes.
Programmatic tests follow the visible pass. After a substantial adjustment,
the affected visible journey is repeated. If an E2E journey is genuinely
blocked or impossible, the exact blocker is retained and independent
programmatic tests still run; a green programmatic result never upgrades the
blocked E2E journey.

## Visible scope represented here

- Document scrolling collapses the Harbor Deck after intentional downward
  travel and restores it on reverse travel or pull-to-refresh.
- Nested scroll containers drive the same collapse/restore policy without
  treating layout-only feedback as user travel.
- Normal motion uses the approved short transition window. With the system
  Reduce Motion setting enabled, the stable hierarchy uses a short crossfade
  instead of spatial movement; the system setting was restored after the run.
- The iPad Workspace Canvas keeps the sidebar and expanded controls reachable,
  and the compact Harbor Deck leaves the page readable.
- DebugLocal sync opt-in remains fail-closed and local: the UI shows
  `Nur lokal`, transport/key actions remain unavailable, the state persists
  over app restart, and cleanup returns the toggle to off. This is not a
  CloudKit, Keychain or cross-device sync result.
- The normal `AhoiMobile-CloudKitDevelopment` scheme opens the regular browser
  UI. This smoke does not invoke the inert CloudKit E2E host and performs no
  real CloudKit mutation. After deterministic project regeneration, the same
  normal-UI test passed again under the scheme's implicit
  `CloudKitDevelopment` configuration.
- An earlier combined iPhone regression run exercised local-only sync opt-in
  and restart together with document scrolling, nested scrolling and
  interactive browser presentations on one build. All 4 focused UI tests
  passed in `/private/tmp/ahoi-mobile-ui-sync-candidate-06.xcresult`; this is
  still dirty-tree simulator evidence rather than a release-candidate receipt.

## Screenshot manifest

| File | Pixels | SHA-256 | Provenance and visible boundary |
| --- | ---: | --- | --- |
| `01-harbor-expanded-document.png` | 1206x2622 | `8d2f0ce4fb8973e45943079738d1bf31b19fadf959656f3b7d696ab3f31c820f` | Expanded iPhone Harbor Deck on the deterministic document-scroll fixture; source video `/private/tmp/ahoi-mobile-harbor-stable-motion-20260830-01.mp4`. |
| `02-harbor-compact-document.png` | 1206x2622 | `164f649261fa22d6c9b1826dc9acb8cc81d0f2364ad7e9ca9194917161b36e9f` | Compact iPhone Harbor Deck after downward document travel; same source run as 01. |
| `03-nested-scroll-expanded.png` | 1206x2622 | `b420bcefb811586e4aedaf2a56c01113f94e9c28a69d779dedc0f655fa1b1a73` | Expanded Harbor Deck on the nested-scroller fixture; source result `/private/tmp/ahoi-mobile-scroll-e2e-fixed-02.xcresult` and video `/private/tmp/ahoi-mobile-scroll-e2e-fixed-20260830-02.mp4`. |
| `04-nested-scroll-compact.png` | 1206x2622 | `0ad25c170af173fecb646bf75c5f2cb48a2d9e6ce0e0666ddf49631c69e6e137` | Compact Harbor Deck after nested-container travel; same source run as 03. |
| `05-reduce-motion-enabled.png` | 1206x2622 | `7a310d4e391d9d9ddabe0d60a933c6843acc1a144b8bd75b3f8b6837c3702941` | iOS Simulator accessibility setting visibly enabled before the journey; source result `/private/tmp/ahoi-mobile-reduce-motion-crossfade-e2e-01.xcresult` and video `/private/tmp/ahoi-mobile-reduce-motion-crossfade-e2e-20260830-01.mp4`. |
| `06-reduce-motion-compact.png` | 1206x2622 | `7f0473e1d78a63c19bfe98213102c8cfdbbeb7f102c0360e1780d3811cc6575f` | Compact Harbor Deck in the Reduce Motion journey; same source run as 05. |
| `07-reduce-motion-expanded.png` | 1206x2622 | `321261b405fe81e64d02e13963bd99ca5e6016aa861959c5558d616d606bc90b` | Restored Harbor Deck in the Reduce Motion journey; same source run as 05. |
| `08-ipad-workspace-canvas-expanded.png` | 2064x2752 | `315fc05b715eff5bda74a9ed679121877646f59120ed07ed1b9b144160c2af3e` | iPad fixture with populated Workspace Canvas, visible sidebar and expanded Harbor Deck; source result `/private/tmp/ahoi-mobile-harbor-stable-motion-ipad-01.xcresult` and video `/private/tmp/ahoi-mobile-harbor-stable-motion-ipad-20260830-01.mp4`. |
| `09-ipad-harbor-compact.png` | 2064x2752 | `a69abd3901b78de9508d8ba9083ddbdf9002747c63def43cccc7dcce2a427ce5` | Compact iPad Harbor Deck with the page and sidebar retained; same source run as 08. |
| `10-sync-local-only-enabled.png` | 1206x2622 | `950d19169cdecb0bcbaffbf109200c3fb6926709f90e605fd6c477c48372aaa0` | DebugLocal settings after local-only sync opt-in; source result `/private/tmp/ahoi-mobile-sync-local-only-03.xcresult` and video `/private/tmp/ahoi-mobile-sync-local-only-20260830-03.mp4`. |
| `11-sync-local-only-persisted.png` | 1206x2622 | `cf242d0313abee975c1b0557cec32537bba2b67cdc67e42c2e8e094057e75ce1` | Local-only state visible after app restart; same source run as 10. |
| `12-cloudkitdevelopment-normal-ui.png` | 1206x2622 | `7d3a5f815b27b0cb9a903f39bb64ae1aec086fd386c09c8ccc66bc830e8fd541` | Regular browser UI from `AhoiMobile-CloudKitDevelopment`; source result `/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-02.xcresult`, video `/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-20260830-02.mp4` and manual capture `/private/tmp/ahoi-mobile-cloudkit-normal-ui-manual-02.png`. |

The MP4 and `.xcresult` paths are transient `/private/tmp` artifacts and are
not checked into Git. The retained PNG hashes permit later integrity checks,
but do not establish candidate identity by themselves.

At the time these stills were curated, the generated project/schemes were
regenerated twice with identical combined SHA-256
`d06d9b5403ab0982bf681a204b84290d8f1464bcde037809523063c46aa35fe0`,
the normal CloudKitDevelopment UI journey was repeated successfully 1/1 at
`/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-03.xcresult`, with log
`/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-03.log` and video
`/private/tmp/ahoi-mobile-cloudkit-normal-ui-e2e-20260830-03.mp4`. Screenshot
12 remains the exact still from the earlier 02 run; the repeat does not change
its provenance or hash.

The later current project regeneration was also a two-run no-op and produced
the updated combined project/scheme SHA-256
`49e503d5da232d2067094999b653096c9e5b923f7a8b8ebd561273a7f7f42643`.
That newer generated-project evidence does not alter the provenance or hashes
of the 12 retained stills.

The earlier combined regression run passed 4/4 focused UI tests at
`/private/tmp/ahoi-mobile-ui-sync-candidate-06.xcresult` with log
`/private/tmp/ahoi-mobile-ui-sync-candidate-06.log`. It covered the persisted
DebugLocal sync opt-in plus both Harbor Deck scroll sources and restoration
after JavaScript and file-input presentations. No additional still was curated
from this run, so the 12-file manifest above remains unchanged.

## CloudKit boundary

Real CloudKit sync was not possible in this state. The current development
profile does not support `iCloud.app.ahoibrowser.AhoiBrowser`, does not include
`com.apple.developer.icloud-container-environment`, and does not match the
requested container identifiers. Simulator-hosted E2E queue/deny tests fail
closed before provider construction because
`com.apple.developer.team-identifier` is absent. Those failures are negative
safety evidence, not CloudKit passes. Closing real sync still requires the
dedicated container/profile, exact entitlements, entitled Mac counterpart,
Keychain lifecycle and a candidate-bound Mac-iPhone/iPad roundtrip with
cleanup.
