# Active Desktop checkpoint

Updated: 2026-09-05. Owner: thread `01a04f97-e3ba-70f2-a031-220b214d352d`.

## Objective and authoritative files

The user commissioned a critical product/technical review on 2026-09-05 and
authorized improvements to the master and overall goal. Follow
`outputs/AhoiBrowser-Master-Zielprompt.md`, starting with its package 1; the
complete browser remains the goal. Findings, rationale and open implementation
items: `docs/reviews/2026-09-05-product-and-execution-review.md`.

The goal tool still stores the earlier Desktop-package-only objective. It
cannot replace that text while active, and Computer Use refuses access to the
Codex app. This is a goal-control limitation, not completion or a reason to
discard the user's revised product objective. Never mark work complete to
force a goal replacement.

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
- Computer Use inventory works again and reported AhoiBrowser stopped. App-
  specific access and the next visible journey must still be exercised.

## Next actions

The review, master corrections, archived chronology and `PERF-04` clarification
are complete as documentation. JSON validity, all 412 unique registry IDs,
unchanged master test-ID coverage and whitespace were checked. No runtime pass
or browser rebuild is implied.

1. Review package-1 source gaps from the new review: preserve valid custom folder
   icons, unify workspace entry points and owned motion, reliable quit behavior,
   compact import/Classic details. Bundle executable corrections once; retain
   all-five-checkbox visual coverage while the existing surface remains.
2. Obtain bookmark-owner handoff and use one clean integrated source snapshot.
   Reuse any appropriate complete candidate; otherwise apply the exact overlay
   and run one combined browser/focused-test-target build. Keep its real handle,
   terminal outcome and receipt; do not infer success from progress lines.
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
