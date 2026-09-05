# Active bookmarks / Chromium coordination

Updated: 2026-09-05. Owner: thread `01a06d69-1034-7372-b784-0b05a53c87e0`.

## Resumed coordination

The user resumed this goal. A fresh CPU check found Unity near 1% and its
compiler workers idle, so the earlier CPU gate has cleared. Exact disk readback
first found 125,586,816 KiB (119.77 GiB), below the hard 120-GiB roll floor despite
`df -h` rounding to 120 GiB. A later readback after retiring the old owned source
snapshot found 126,506,552 KiB (120.65 GiB). Only 53 MiB of the change is explained
by this thread's cleanup; do not attribute unrelated free-space changes to it.
Recheck exact space before the roll. The mobile collection decision is pending.

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
build was started. Final build, E2E, behavioral tests and actual roll remain open.

The `4e2458a` retry log now ends with Ninja failure. Its only reported compile
error is the test's nonexistent `components/bookmarks/browser/bookmark_metrics.h`;
the pinned source provides `components/bookmarks/common/bookmark_metrics.h`.
Browser bundle copy and the large browser/interactive-test links completed, but
this is not a green guarded build or signed/installed candidate. Diagnostic and
bounded fix ownership were queued to Desktop in message
`01a07024-7041-7832-b0f4-ab8af0ce96bf`; the bookmark owner remains read-only for
product/test files during that retry. Do not poll old build session `22604` as
if it were still running merely because a checkpoint has not caught up.

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
fixes, have reached product compilation in the combined build; the test-target
compile failure still prevents a successful guarded-build receipt. The build
must include new patch `0030` and the new context-menu source files. Use the
current ordered series, not an older snapshot. The earlier three-object compile
pass applies only to `124429a`. Do not install incomplete `out/AhoiDev` output
as if it were a verified final candidate.

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
evidence; the final fixes still need compilation and visible acceptance.

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
- No final bookmark executable, visible E2E or bookmark behavior-test pass exists.
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
8.9 GiB by `du`; all source commits are ancestors of current HEAD. The immediate
rollback named by the current installation receipt is `f1475d6` and must remain
available. Even deleting all eight would not bridge the gap, and APFS shared
blocks may reduce actual reclamation. Nothing was deleted. Do not weaken the floor or delete
another owner's output. Actual pin/sync, unmodified upstream control, Ahoi build,
installed runtime and focused post-E2E regression remain open.

## Other pending input / resources

The user has been asked whether Desktop and Mobile should expose the same
bookmark collection. Current Mobile Library contains Ahoi workspace saved pages;
it does not prove access to Chromium bookmarks. No new mobile schema or sync
migration has been started without that decision and the Mobile owner's handoff.

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
