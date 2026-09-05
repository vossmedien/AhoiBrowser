# Active bookmarks / Chromium coordination

Updated: 2026-09-05. Owner: thread `01a06d69-1034-7372-b784-0b05a53c87e0`.

## Current next action and evidence — 2026-09-05

**NEW BINDING USER DIRECTION:** the app is pre-launch and not actively used.
Implement ONE current format for every allowed sync data class on iOS/macOS,
without an elaborate migration or permanent v2/v3 operation. ADR 0009 and
`config/sync-format.json` supersede the older mixed-version instructions below.
Fresh isolated stores/test zones are the acceptance basis; no old profile,
journal or server data is silently deleted. Common C++/contract remains here,
Swift is now also owned by this unified Sync implementation thread. The former
Mobile owner is the coordinator/read-only reviewer. Native Tree/Session/build
remains Desktop-owned; no further Swift ownership request is required.

The current `225df88` correction build stays immutable with Desktop. It is a
compiler/UI baseline, not the final unified-format candidate. Its three Common
test-API corrections are separately committed/pushed in
`22e2f2b7a3f5b0832cc7eff3d23819a6041aa737`, without product/schema/consent changes.
No new build/test/runtime was started here. The new format contract is not yet
implemented or accepted merely because its JSON/ADR exists.

Current correction status: combined Desktop build `90068` / source `dc01cb5`
is terminal EXIT1, not a new candidate. Its three common compile causes are
fixed separately in pushed `3035529d21fd03ebdf29977ecbe5de842261e6cb`; this is the
exact reviewed three-file proposal, excluding all Consent WIP. Desktop's own
fourth cause is `1ea90da`. No Root build, checkout refresh or install was started.

The separately confirmed local consent race is now corrected in source with
effective provider/backend generation checks before journal commit, native
projection apply and ACK, plus checks after the provider-to-pump task hop.
Nine new regressions are written, none built or run. The original projection
scope stays invalid after reapproval; local data/outbox are preserved, not
retroactively deleted. Details and exact source scope:
`docs/audit-evidence/2026-09-05-bookmark-consent-generation-fix.md`.
Exact correction freeze: `225df88f4e6dbe692390f52b8687b653541723ac`, committed and
pushed; Desktop integration handoff `01a07231-8c67-79e2-b983-2e38c5a2e2f8`.
All own product WIP is clean after this 25-file handoff. Do not apply the older
three-file proposal again or infer runtime acceptance from the new source.
This is a new bounded source handoff, NOT a cross-account/E2E pass. The larger
Bookmark/capture/v3/Chromium acceptance gates remain open; Desktop retains the
one combined build/install/UI lease.

- Installed Desktop is now `3d413efb5b6f196403e92f51631c346c9c55b2e5` on
  Chromium `.65`. Desktop built/installed its nested-tree persistence correction;
  receipts are in its checkpoint. The new Bookmark sync code below is NOT in
  that candidate.
- Desktop explicitly handed over isolated-profile Bookmark UI access in
  `01a0708d-ebf7-7da1-bc02-70b4026b9da6`. Fresh `cua.getState()` failed before
  app access with `Sky Computer Use native pipe startup failed`; a reset of
  this thread's own JS session and a fresh inventory failed identically.
  No app was launched and no profile was changed. UI was explicitly returned
  to Desktop in `01a0709c-556c-70c1-b59b-17fb3d9cdc30`. Do not assume this thread
  still holds UI access. Checkout/build/install remain Desktop-owned.
- Both temporary Bookmark UI and Mobile My-Mac windows were explicitly returned
  and accepted by Desktop. No old slot remains reserved. Desktop owns app/UI,
  checkout/out and build/sign/install. Its checkpoint at `e6a7e66` waits for this
  source-freeze for one combined build; do not start a competing build or act on
  delayed repetitions of earlier offers.
- A further explicit UI offer was checked at 11:44 CEST: installed plist still
  `3d413ef`, but a read-only CUA inventory again failed before app access with
  the same native-pipe error. No app/profile/UI action followed. That offered
  slot was explicitly returned as well; the newer Mobile runtime reservation
  was not overridden. Do not retry merely for another delayed copy of the grant.
- Under the documented technical-E2E exception, the already-built
  `SidebarBookmarkShelfViewTest.*` ran on `3d413ef`, one job, zero retries:
  **11/11 SUCCESS**, process exit 0, about six seconds. This is NOT visible E2E.
  Evidence: `artifacts/tests/bookmarks-3d413ef-20260905/{run.log,summary.json}`.
  Test executable SHA-256:
  `4072e793f28643cd40a1d4a4c45ebeb4b68246cd4601688ddf48ba3fab5eeeca`;
  summary SHA-256:
  `fec00796a2518713367e4edb5f7b504b5e9877412e4d103aee71fc62bb2ac176`.
  The component runtime emitted an `ANGLESwapCGLLayer` duplicate-class warning;
  it did not fail these tests and is not a diagnosed cause of the screenshot.
- Current test source has later commits `55187e8` and `192aad9`: all three
  viewport/offset assertions now use `ScrollView::GetVisibleRect()` directly;
  product anchors correctly retain `View::GetVisibleBounds()`. These commits
  are after `3d413ef`, so the recorded 11/11 does not cover that refinement.
  Rebuild only in the next coherent package and rerun the two affected focus/
  overflow tests after its relevant visible journey (or a freshly documented
  technical-E2E exception). No additional build/test ran for the refinement.

### Native shared-bookmark package: source freeze, no compiled acceptance

The Bookmark owner owns common C++ sync model/codec/merge/SQLite/provider,
native BookmarkModel adapter and identity journal, explicit consent UI, GN,
`config/sync-policy.json`, ADR 0006/0008 and canonical goldens. This bounded
package is frozen in `c28ec4af0d46c0d1b62a8ea67d95cf5a0c05aff8`, committed and
pushed as compile-candidate source, NOT a tested feature or release. Explicit
Desktop build handoff: `01a0718c-6641-7821-837f-7190f517ad0d`; Mobile coordination:
`01a0718c-66c2-7a50-ab34-b84f9fd3aa42`. These 60 owned files stay unchanged until
combined-build feedback. No shared-checkout refresh, build, behavior test, app launch or install
was performed by this source wave. The installed plist was freshly read as
`3d413ef`; it contains none of the new adapter/consent implementation.

Current source has Bookmark entity 11, atomic location, iterative graph
validation, wire-v2 codec and transactionally migrated SQLite schema 5. The
native adapter uses storage-qualified identities and pre-apply receipts in the
existing SyncStore. Native MetaInfo is checked against that ledger and logical
identity; copied tokens do not grant identity. Baseline/receipt/session separation
protects local edits when an in-memory native apply has not reached Chromium's
JSON save. Pre-move keys, move-then-delete, explicit Undo/new identity, subtree
deletion and account detachment have focused regression source.

Category approval is local/default-off and separate from global Sync. It guards
native seeding, outbox dispatch and CloudKit inbox decryption/recovery; revocation
retains opaque data and pending work rather than fake-acking. Account changes
invalidate prior approval. Existing engine/container/crypto stay authoritative.
The control distinguishes approval from readiness and shows reconciliation
failure. Unsupported local content pauses Bookmark reconciliation without
exporting credentials, rewriting native data, or replacing a failed local
mutation with an older projection. Other categories remain independent; this
conservative pause is not a partial-sync success.

Independent source review found a real double-owner BubbleDialogModelHost bug;
the Widget is now its sole owner. All 95 new sync tests are source-only,
including 14 native adapter cases. Three additional Views/TestingProfile popup
tests cover Cancel/reopen, owner teardown and category-only approval, also NOT
RUN. No count here is a test pass. File/test handoff and remaining
risks: `docs/audit-evidence/2026-09-05-native-bookmark-sync-source.md`.

Matching Mobile Bookmark source `313e3518c1d6daa8ee4d6a3ae25a01ec99a0d39b`,
DebugLocal 0.1 (15), has separate candidate-bound visible acceptance 1/1; then
70 Core passes / 2 entitlement skips, 36 shared Swift and 2 event-contract passes.
See Mobile's checkpoint and canonical receipts. This does not prove native
Chromium roundtrip, real cross-device keys or Production. Bookmark Golden
`ae38ed7` uses valid UUID device strings and raw positive Int64 Windows-epoch
creation metadata, including pre-1970 values; wire-v2 stays unchanged.

ADR 0007 shared-normal-tab Swift remains Mobile-owned. Native Tree/Session/
`tab_tree_sync_adapter` remains Desktop-owned. Common C++ v3 implementation is
still future work; no global v3 writer is enabled. ADR 0008 and the canonical
`shared_tab_wire_v3_contract.json` are frozen in `09cae9f`: v2 Capability12,
exact maps/IDs, conservative verified-peer gate, legacy absence/Bottom sentinel,
and atomic web/new-tab/local-only target metadata. The fixture is a contract,
not executed C++/Swift or live activation evidence. Mobile/Desktop handoffs:
`01a07159-6d94-7ab3-bd9b-cb56c1606056` and
`01a07159-6dfd-7391-8672-c20aedc8453f`.

Next: hand the exact committed Bookmark package to Desktop for its single guarded
combined build, including `ahoi_sync_unittests` and shelf/control tests. Visible
Bookmark E2E needs that exact runnable candidate and a fresh explicit UI handoff;
only a freshly established technical E2E gate permits independent programmatic
checks without visible acceptance. Actual Chromium roll, shared collection
roundtrip, folder-motion/rendering acceptance and final tested delivery remain
open. Android was a future-options question, not a new build.

## Earlier coordination (historical)

Latest roll resource readback, 2026-09-05 12:34:56 UTC: all eight target objects
for added patches 0030-0033 are locally present at the previously reviewed .83
candidate; no download or checkout mutation was needed. The series now has 33
entries, and its full new preflight is NOT RUN. A competing identified FillIt
Unity review job measured 106.9%, 124.0%, then 133.6% CPU; no intensive job was
started here. Exact free space was 92,064,208 KiB = 94,273,748,992 bytes, about
87.8 GiB, below the hard 128,849,018,880-byte roll floor. This supersedes the
older near-120-GiB samples below. Receipt:
`artifacts/build/bookmarks-chromium-20260905/roll-readiness-20260905T123456Z.json`.
This is resource/object availability, not a current online discovery, patch
applicability result, actual roll, or permission to touch the Desktop lease.

The user resumed this goal. A fresh CPU check found Unity near 1% and its
compiler workers idle, so the earlier CPU gate has cleared. Exact disk readback
first found 125,586,816 KiB (119.77 GiB), below the hard 120-GiB roll floor despite
`df -h` rounding to 120 GiB. A later readback after retiring the old owned source
snapshot found 126,506,552 KiB (120.65 GiB). Only 53 MiB of the change is explained
by this thread's cleanup; do not attribute unrelated free-space changes to it.
After installation, readback was 125,553,560 KiB (119.74 GiB), below the floor
again. Recheck exact space before the roll.

## New binding user decisions — 2026-09-05

The user explicitly answered **yes** to the same Desktop/Mobile bookmark
collection after being told that additional coordinated sync work is required.
The mobile collection question is resolved; the existing Workspace Library is
not sufficient. The goal contract now requires an adapter from native Chromium
bookmarks into the existing encrypted Ahoi sync/domain path, with a matching
Mobile projection, stable identities and lossless migration/merge behavior.
No new Production-CloudKit or release authority is inferred.

The user also reported that sidebar folder expand/collapse animation is not
pleasant. The stated working assumption is normal Desktop workspace folders.
Desktop was asked to own reproduction and correction in its existing tree/motion
modules after the current import step, with visible acceptance before tests.
The bookmark owner will not edit `sidebar_tree_view*` or its animation modules
in parallel. Coordination messages:
Desktop `01a0704e-c481-7f61-8cfd-7cb913353072`,
Mobile `01a0704e-c41f-7393-9b16-571d3454d397`.

For the new shared-bookmark work, the bookmark owner is defining the cross-
platform record/adapter contract in
`docs/decisions/0006-shared-bookmark-collection.md`. A read-only helper checked
native Desktop BookmarkModel identity/root/observer seams; Mobile has a bounded read-only
request for its domain/transport and UI attachment points. Existing record
policies are explicit allowlists and currently have no bookmark record class.
Do not introduce concurrent schema definitions or silently map bookmarks to
workspace saved pages. Current Mobile CloudKit peer tests remain separate scope.

Additional user screenshot at 09:19:18 reports a dark rectangular step at the
sidebar/rounded-toolbar/content seam. The original is preserved byte-for-byte at
`artifacts/computer-use/bookmarks-coordination-20260905/user-sidebar-seam-091918.png`
(SHA-256 `46a7d38216279c5ba984988bcc1c2773ea0d2f638d77658da82379fb568a7158`).
Installed-app readback during triage remains `0a13e22`; new sync-domain source
has not been built or installed. The screenshot is user-reported evidence, not
an acceptance pass. Desktop owns reproduction and bounds/clipping correction;
message `01a07074-0f78-7973-800c-52399f5f1cae` provides the shared image path.
The attempted CLI image attachment was rejected and sent no message; the
successful text message instructs direct viewing of the preserved file.

Direct coordination is now available through the documented `codex queue`
command in installed CLI 0.153.2. Explicit handoff/resume message
`01a07002-cb41-7f50-94e1-9810f5c75753` was queued to the Desktop owner, and
coordination/capacity message `01a07002-cb41-7ca0-8c20-6180b745b413` to Mobile.
The Desktop owner's updated checkpoint confirms receipt and ownership. Its
first combined build at `a453dee` ran and stopped on the new bookmark code's
incorrect `View::GetVisibleRect()` calls; `87a5999` corrects the View API. The
owner also fixed the malformed final hunk count of patch `0030` in `a453dee`.
The bookmark owner explicitly delegated these bounded compile fixes and remains
read-only for the files during the retry. An API note was queued: viewport
offset tests should use `ScrollView::GetVisibleRect()` directly, whereas source
View clipping uses `View::GetVisibleBounds()`.
Follow the Desktop checkpoint for the new live build handle and exact receipt;
the old first-attempt handle is terminal. No duplicate writer or competing
build was started. Installed E2E, behavioral tests and the actual roll remain open.

The `4e2458a` retry failed only on the test's nonexistent
`components/bookmarks/browser/bookmark_metrics.h`; Desktop corrected the include
to `components/bookmarks/common/bookmark_metrics.h` in `0a13e22` under the
delegated compile-fix ownership. Old build session `22604` is terminal.

The guarded combined `0a13e22` build then reached its success marker and emitted
its final provenance at `2026-09-05T06:18:05Z`. Its PID `48861` is gone; Desktop
owns the terminal receipt for exec session `47698`. The bookmark test executable
compiled and linked, but no behavior test has run. Native development signing,
nested signature verification and portable runtime staging completed. Readback
of the built bundle's Info.plist and receipt agrees on source
`0a13e22ff4e9cd1ac43a508e304fcaa0fe64a997`, Chromium `152.0.7977.65`, and clean
source state. The receipt is preserved byte-for-byte at
`artifacts/build/bookmarks-chromium-20260905/ahoi-dev-build-0a13e22.json` (SHA-256
`5df3b22a8b6822edebd16891343bafd65c2acb89c84e19433dec0e2a658812e2`).
Installation has now completed through the guarded atomic swap. Receipt:
`artifacts/install/ahoi-dev-0a13e22-20260905T062114Z.json`. Direct installed
Info.plist readback reports `0a13e22`; receipt comparison confirms identical
source, Chromium commit and bundle-tree hash with the built candidate, successful
post-install verification and `releaseEvidenceEligible=false`. The immediate
rollback is now `/Applications/.AhoiBrowser.rollback-v0.0.1-b1-s1f5f22fbfe26-h8cf4497233a8.app`.
UI ownership remains with Desktop for its first visible import-dialog check.
A short exclusive bookmark E2E slot was proposed in queue message
`01a0702d-4fd0-7730-8657-3a4018949a99`; do not act before explicit handoff.

## Explicit shared-checkout / build handoff

**RELEASED to Desktop owner `01a04f97-e3ba-70f2-a031-220b214d352d`.**

The bookmark owner has frozen product source at
`fafd776bdf87e13f1e79959c8a8c17b7aa02d118`, on the shared branch
`codex/desktop-core-feature-wave-20260830`. This includes Desktop package
`fe0afa4`, earlier bookmark source `124429a`, and composer optimization
`5a763c2`. There is no active bookmark-owned build, fetch, hydration, preflight
or source mutation in the shared Chromium checkout. Old handles below are
terminal. The Desktop owner may update its existing clean snapshot to this
integrated source, refresh the overlay and run the combined guarded build.

The final bookmark corrections in `fafd776`, with Desktop's bounded API/patch
fixes, are present in the guarded combined `0a13e22` build. New patch `0030`, the
context-menu source files and bookmark test target are integrated. The earlier
three-object compile pass applies only to `124429a`; use the exact new receipt
and installed bundle for acceptance, not an older incremental output.

Suggested combined target: `./scripts/build-ahoi.sh dev ahoi_sidebar_tree_unittests`
plus the Desktop owner's already selected focused targets. Build/sign/install
once. Then perform visible Desktop and bookmark journeys before executing the
focused bookmark suite:

```text
ahoi_sidebar_tree_unittests --gtest_filter=SidebarBookmarkShelfViewTest.* --test-launcher-retry-limit=0 --test-launcher-jobs=1
```

The bookmark owner will not switch the Chromium pin, refresh the shared checkout,
or start a competing build/install until the Desktop owner hands it back.
The installed app and user profile remain under the Desktop owner's current
visible acceptance. Bookmark E2E will use that exact installed candidate with
an isolated test profile after an explicit UI handoff.

## Current contract and owned source

Contract: `outputs/AhoiBrowser-Lesezeichen-Chromium-Zielprompt.md`.

Owned: `sidebar_bookmark_*`, their focused tests, shelf placement/tokens,
the `CanStartBrowserWorkspaceGesture` seam, patch `0030`, scoped composer
optimization, Chromium roll candidate/preflight, this checkpoint and evidence.
Mobile code remains with `01a044d6-1545-7532-8394-6b7df1144bb1`.

The final source provides labelled horizontal bookmarks, native lazy folder
menus and context actions, visible native overflow arrows, stable buttons across
rename/reorder, targeted favicon updates, frame/generation-bound focus reveal,
and suppression of late context menus after the source hides or disappears.
Default bookmark activation opens a normal foreground tab so an active saved
Ahoi page does not acquire the bookmark's URL. Standard modifier dispositions
remain intact. Shelf scrolling cannot initiate a workspace swipe.

Patch `0030` is a small optional native context-menu presentation hook after
the asynchronous clipboard check. It avoids a global test hook and preserves
Chromium's bookmark model, clipboard capability, permission and mutation owner.
Stock bookmark-bar/Apps/tab-group presentation options are hidden only in the
Ahoi shelf's context menus.

Two independent read-only reviews found the now-addressed overflow, direct
context, focus, stale-count and delayed-callback gaps. Their findings are source
evidence; the integrated fixes have now built but still need visible acceptance
and subsequent focused behavior tests.

## Verified evidence, with limits

- `5a763c2`: 7 composition and 18 overlay-state safety tests passed, including
  exact bytes/modes/symlinks, real-index/object-store isolation and rollback.
- The optimized verification returned in 63.88 seconds in one observed run
  (the prior implementation had an observed 36-minute phase). That run correctly
  rejected the interrupted build's temporary Rust workaround. Full tree
  comparison isolated it; Rust and V8 helpers were restored to their configured
  original SHA-256 values. No Ahoi product work was rolled back.
- Overlay refresh and three bookmark product object compilations succeeded
  before the final review corrections. Logs:
  `artifacts/build/bookmark-shelf-review-apply-20260905.log` and
  `artifacts/build/bookmark-shelf-compile-20260905.log`.
- The integrated executable and test target are now built as recorded above;
  no installed bookmark E2E or bookmark behavior-test pass exists.
  Old sessions `72089`, `67371`, `42586`, `10875`, `96812`, `73207`
  are terminal; do not poll/restart them as live work.
- Retired the old owned 53-MiB build snapshot
  `/private/tmp/ahoi-bookmark-shelf-build.4MV9hC/repo` at `ab2e709` after verifying
  ancestor status, clean tracked/index state and absence of an active working
  directory there. The only unique ignored files were two tooling receipts and
  regenerable Python bytecode. Both receipts were copied byte-for-byte, with
  matching SHA-256, to
  `artifacts/build/bookmarks-chromium-20260905/retired-ab2e709/` before removal.
  Source remains in Git. No other owner's snapshot, output or backup was removed.

## Chromium update: prepared, not performed

Current production pin remains `152.0.7977.65` / `fc4d67f1788019a27e32511137ceccbd2fafdaaa`.
Official discovery at `2026-09-04T22:42:36Z` selected fully rolled Mac-ARM64 Stable
`152.0.7977.83` / `79460ebecaa5625e57a5fb679a735659e73dc687`.
The explicitly reviewed binding is `config/upstream-roll-candidate.json`;
its SHA-256 is `0f2a8edd558279780b81ef9be5913d8aa700e7cd4b7f3d7d0c2118fccb751d5f`.

A fresh online discovery at `2026-09-05T06:15:25Z` independently confirmed the
same fully rolled version and commit. Evidence:
`artifacts/build/bookmarks-chromium-20260905/candidate-refresh-20260905T0615Z.json`,
SHA-256 `68fefec5df72e59c919d481e285c33abe6bde0cb1eefff7c2fa3e0462b250dda`.
The reviewed candidate binding and production pin remained unchanged. This was
only official metadata discovery, not a checkout fetch, hydration or pin switch.

Filtered tag/tree fetch succeeded. Hydration covered 304 paths with 298 existing
blobs and only 6 downloads (2,477,622 decoded bytes). The 29-patch preflight
passed with zero conflicts/already-upstream patches and unchanged HEAD/index/
worktree. It predates new patch `0030` and the final standalone context changes;
refresh the final preflight after source freeze and before a real roll.

Reports are preserved under `artifacts/build/bookmarks-chromium-20260905/`.
DEPS changes V8, Skia, ANGLE, DevTools frontend and Dawn. Direct official Gitiles
readback of the candidate's Rust wrapper and V8 Inspector-Protocol generator
confirmed byte-identical original SHA-256 values against both configured
workarounds. The candidate V8 commit is
`4323497a6a73839e6d5260f6acd7ec0212cb3321`; the revision bindings still need
updating during the real roll. Evidence: `dependency-workaround-review.json`
beside the reports. No workaround or production pin was changed.

The earlier reserve audit at about 85 GiB found only the active `AhoiDev` output;
this thread's disposable files could not recover the then roughly 35-GiB gap.
Free space has since changed; the latest exact readback is recorded above and
must be refreshed before switching the pin. The read-only inventory also found
eight old `/Applications/.AhoiBrowser.rollback-*.app` bundles totaling about
8.9 GiB by `du`; all source commits are ancestors of current HEAD. The then
immediate rollback was `f1475d6`; the new installation's immediate rollback is
recorded above. Both remain preserved. Even deleting all eight would not have
bridged the earlier gap, and APFS shared
blocks may reduce actual reclamation. Nothing was deleted. Do not weaken the floor or delete
another owner's output. Actual pin/sync, unmodified upstream control, Ahoi build,
installed runtime and focused post-E2E regression remain open.

## Other pending input / resources

The former mobile decision gate is resolved as recorded above. Current Mobile
Library contains Ahoi workspace saved pages, not Chromium bookmarks. The shared
bookmark implementation now requires the coordinated domain/adapter work;
historical Library evidence must not be upgraded to a pass for that feature.

Mobile supplied historical Library evidence at
`artifacts/e2e/mobile-library-6405167/`. Readback confirms both clean source and
embedded source `640516791522e7604d5619bee354420f2cc56b7e`, DebugLocal `0.1 (1)`,
and the real `testSavePageToWorkspaceThenOpenFromTreeAndLibrarySearch` pass in
162.347 seconds; the surrounding log reports 5/5. This proves the historical
workspace-library journey, not Build 10 or a shared Chromium collection.
Mobile closure commit `760e00a` remains frozen. Its owner reports removal of
about 330 MiB of unused owned caches only; do not attribute the entire disk-space
increase to that cleanup. Products, archives and evidence remain preserved.

The earlier FillIt Play-mode CPU gate has cleared. No foreign process was
stopped. Recheck actual workload ownership and CPU before every heavy phase;
absence of a compiler process does not return Desktop's checkout/UI lease.
