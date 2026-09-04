# Active bookmarks / Chromium coordination

Updated: 2026-09-05. Owner: thread `01a06d69-1034-7372-b784-0b05a53c87e0`.

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

The final bookmark corrections in `fafd776` have **not yet compiled or run**.
The build must include new patch `0030` and the new context-menu source files.
Use the current ordered series, not an older snapshot. The earlier three-object
compile pass applies only to `124429a`. Do not install the existing incomplete
`out/AhoiDev` output as if it were the final source.

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
- Disposable old build snapshot:
  `/private/tmp/ahoi-bookmark-shelf-build.4MV9hC/repo` at `ab2e709`.
  It is historical, not current build authority. Preserve only needed receipts
  before removing this owned 53-MB worktree later.

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
DEPS changes V8, Skia, ANGLE, DevTools frontend and Dawn. The V8 build-workaround
revision must be revalidated/rebound; no workaround or production pin was
blindly changed.

The last free-space check was about 107 GiB, below the configured hard 120-GiB
checkout floor. Only the active `AhoiDev` output exists; this thread's disposable
files cannot recover the roughly 13-GiB gap. Do not weaken the floor or delete
another owner's output. Actual pin/sync, unmodified upstream control, Ahoi build,
installed runtime and focused post-E2E regression remain open.

## Other pending input / resources

The user has been asked whether Desktop and Mobile should expose the same
bookmark collection. Current Mobile Library contains Ahoi workspace saved pages;
it does not prove access to Chromium bookmarks. No new mobile schema or sync
migration has been started without that decision and the Mobile owner's handoff.

The latest confirmed foreign CPU gate was FillIt Unity Play mode, PID `15530`,
about 99% CPU. Command, ancestry, working directory and
`OpenPlayablePreview -> EditorApplication.isPlaying = true` were checked.
The user was asked to have its owner stop the preview temporarily. Never stop
that foreign process here; recheck actual workloads before a build.
