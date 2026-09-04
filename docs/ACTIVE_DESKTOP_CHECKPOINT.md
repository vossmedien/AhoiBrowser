# Active Desktop checkpoint

Updated: 2026-09-05. Owner: thread `01a04f97-e3ba-70f2-a031-220b214d352d`.

## Objective and authoritative files

The user commissioned a critical product/technical review on 2026-09-05 and
authorized improvements to the master and overall goal. Follow
`outputs/AhoiBrowser-Master-Zielprompt.md`, starting with its package 1; the
complete browser remains the goal. Findings, rationale and open implementation
items: `docs/reviews/2026-09-05-product-and-execution-review.md`.

The user has now replaced the registered goal. A live `get_goal` read confirms
the full master/Definition-of-Done objective, beginning at this checkpoint.
The earlier goal-control limitation is resolved; do not request that action again.

## Current source and ownership

- Canonical repo: `/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser`.
- Branch: `codex/desktop-core-feature-wave-20260830`; reviewed product baseline
  `d6cfa71`. Read current Git HEAD; later documentation commits do not change the
  installed product.
- Committed presentation package `b045dbf`: all-five-checkbox layout,
  caret-free open/closed folders, sidebar-only committed slide,
  same-WebContents fade suppression and early product split-feature enablement.
- Later integrated shelf commits: `41c6856`, `c3f599a`. Preserve them. The old
  clean `b045dbf` worktree is a historical source snapshot, not the next build
  authority after these integrations.
- Bookmark components and overlay-composer performance changes are owned by
  thread `01a06d69-1034-7372-b784-0b05a53c87e0`. Read
  `docs/ACTIVE_BOOKMARKS_CHECKPOINT.md` before using the shared Chromium checkout
  or `out/AhoiDev`; explicit handoff is required.
- Mobile remains owned by thread `01a044d6-1545-7532-8394-6b7df1144bb1`.
  Preserve its dirty files and use `docs/ACTIVE_MOBILE_CHECKPOINT.md` for handoff.

## Installed candidate and build state

- `/Applications/AhoiBrowser.app/Contents/Info.plist` was read live and reports
  `1f5f22fbfe26069572a2861ecaf7304a25f82a54`.
- Existing matching receipt:
  `artifacts/install/ahoi-dev-1f5f22f-20260904T170613Z.json`.
  Full bundle hashes were not recomputed during the review.
- The installed app has no proof of `b045dbf` presentation changes.
- Foreign build parent PID 51290 and verifier 72607 are gone. The bookmark
  owner confirms no new-source successful build receipt; do not install its
  incomplete output or continue polling those dead handles.
- No current build is owned by this thread. Recheck live CPU/ownership before
  the next CPU-intensive phase. Process absence is not checkout handoff.
- Computer Use can select and raise the installed app. Current old-candidate
  baseline is open on the read-only Arc preview; no import was committed.

## Package-1 integrated source; build and acceptance pending

- Computer Use successfully selected and raised the exact installed app. It
  began with an empty window. `AhoiBrowser > Lesezeichen und Einstellungen
  importieren…` was visible and enabled, but clicking it left the window empty.
  The command exists in the App menu; do not add a redundant File/Bookmarks entry.
- The cause is Chromium's unconditional `kNoTab` early return in
  `BrowserCommandController`. New patch `0028` allows explicitly enumerated
  window/profile commands in constructed normal windows and retains the guard
  for page-dependent commands and other window types. It also changes only the
  fresh local-state default for hold-to-quit; explicit stored choices survive.
- Folder rows now resolve the existing named icon choices and a bounded single
  printable Unicode grapheme as an emblem while retaining open/closed folder
  silhouettes. Unknown names fall back. The obsolete caret indentation is gone;
  same-depth page and folder title columns align. No runtime pass yet.
- A browser regression exercises the actual import command dispatcher from an
  empty window and expects one real Settings tab, rather than calling the
  working destination function directly. It has not yet run.
- Direct Settings entry successfully opened the Arc preview on installed
  `1f5f22f`: 1 workspace, 36 folders, 133 pages and 3 splits. All five checkbox
  boxes visibly sat above their labels; the user's 2026-09-05 screenshot confirms
  the same defect. The prepared UI retains three actual profile/category choices,
  aligns controls with the first text line, and replaces duplicate consent with
  a mandatory-backup notice and one explicit Import action. Patch `0029` carries
  the matching English/German message. The backend transaction is unchanged.
- Dot/index and relative workspace changes now share the committed transition.
  Only scroll contents move inside the viewport; header, shelf and footer stay
  fixed. Changed WebContents fade without transform mutation. Cancel preserves
  unrelated layer properties. Reduced Motion uses an explicit opacity duration;
  its regression disables rich animation to match actual macOS behavior.
  Continuous gesture preview/commit/cancel remains an open WS-09 follow-through;
  this post-commit transition is not a full physical-gesture pass.
- uBO's initial dialog keeps source/version/readiness and permission notices;
  full provenance is in a localized, accessible, bounded Details section.
  Service, consent, CTA and native-sheet handoff remain unchanged.
- Independent read-only review caught the reduced-motion duration bug, now
  corrected; no other concrete blocker found. Scoped patch application checks
  and formatting succeeded. No new binary or programmatic test pass is claimed.

## Next actions

The review, master corrections, archived chronology and `PERF-04` clarification
are complete as documentation. JSON validity, all 412 unique registry IDs,
unchanged master test-ID coverage and whitespace were checked. No runtime pass
or browser rebuild is implied.

1. Commit only this Desktop package's source and this checkpoint, then make one
   clean integrated build snapshot. Keep foreign Mobile files and guidance out
   of the commit. Source freeze is not runtime acceptance.
2. Obtain bookmark-owner handoff and use one clean integrated source snapshot.
   Reuse any appropriate complete candidate; otherwise apply the exact overlay
   and run one combined browser/focused-test-target build. Keep its real handle,
   terminal outcome and receipt; do not infer success from progress lines.
   Extra targets: `ahoi_startup_policy_unittests`, `ahoi_arc_import_unittests`,
   `ahoi_arc_import_browsertests`, `ahoi_sidebar_tree_unittests`,
   `ahoi_extension_policy_unittests`, `ahoi_extension_ui_unittests`,
   `ahoi_ubo_browsertests`, `browser_tests`, `interactive_ui_tests`.
3. Install atomically, then perform visible Arc/menu/checkbox/folder/workspace/
   zero-tab-split/quit journeys. Real Arc import must include valid 2/2/3 splits,
   result, restart and an identical second no-op import. Preserve the source and
   prior recovery backup/journal until the corrected flow passes.
4. AnyChat: ordinary Store path, cancellation, success, action/Side Panel,
   shortcut, disable/enable and restart. Classic: one-click official pinned CRX,
   Chromium permissions, real filtering, dashboard and restart. Lite removal
   occurs only after Classic readiness and the deliberate migration action.
5. Run focused programmatic regressions after visible behavior, document exact
   evidence, commit/push own changes, then advance to the next master package.

## Historical evidence

Earlier candidate/test/crash chronology is preserved in
`docs/audit-evidence/2026-09-04-desktop-checkpoint-history.md`.
Its 173-test development matrix and Arc recovery attempts are not current
candidate or full-product passes. AnyChat and Classic installation status must
be read again at their actual install step. Existing profiles and secrets are
not review artifacts.
