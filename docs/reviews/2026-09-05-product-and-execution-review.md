# AhoiBrowser product and execution review — 2026-09-05

Requested by the user after the model change. Inspected committed source
`b045dbf` and the subsequently integrated canonical `d6cfa71`, the full master
prompt, current checkpoints, build scripts and selected implementation paths.
This is a review of observable work, not a comparative benchmark of models or
an assertion that every prior decision came from a particular model.

## Findings and decisions

| Finding | Evidence | Decision / remaining proof |
| --- | --- | --- |
| Product objective drift | Registered thread goal ends after one Desktop package; the user explicitly requires the complete browser. The old master also ends with an unconditional Phase-0 restart. | Master now preserves the full finite v1 contract, names the current package first and resumes from the checkpoint. The registered goal text still needs replacement through the product's supported goal control. |
| Contradictory product requirements | Master required a native mobile browser and later excluded a full iOS browser; tint default was both on and off; a three-entry patch claim contradicted `series`. | Contradictions removed in place. Mobile remains in the overall goal with its separate owner. Tint remains on by default, matching `appearance_prefs.cc`, while explicit user choices remain authoritative. Patch order comes from `series`. |
| Development work repeatedly treated as release proof | Master demanded DMG/notarization before every CU journey; `tools/evidence.py` and `requirement_audit.py` validate the full release chain and the currently checked-out Git SHA. Existing development results consequently cannot constitute release passes. | Keep release checks strict. Record development acceptance separately and link it to its actual candidate. A documentation or unrelated Mobile commit must not require a browser relink or rewriting old receipts. Supporting audit-tool changes remain an implementation item. |
| Review scope encouraged repeated audits | Old recovery instructions demanded a full best-practice audit before every build and implied that a small fix was insufficient. Checkpoint accumulated contradictory next actions over 322 lines. | Review changed risk boundaries once per package. Preserve a short current checkpoint and move old chronology into an explicitly historical document. Small complete fixes are valid. |
| Overlay composition amplifies build cost | At reviewed `d6cfa71`, `compose_overlay.py::add_overlay_file` runs `hash-object` and `update-index` separately for each overlay file; 748 files were enumerated. `build-ahoi.sh` verifies before hooks, after hooks and after compilation. A verifier was observed live for over 36 minutes. | Batch isolated index updates and measure each phase while preserving identical expected trees, patch deltas, file modes, symlinks and real-checkout immutability. This work is now explicitly owned by the bookmark thread; do not duplicate it or claim an unmeasured speedup. |
| Newer integrated candidate was overlooked by a frozen snapshot | `41c6856` and `c3f599a` integrate the bookmark shelf after `b045dbf`. PID 51290 is terminal and no successful new-source build receipt exists. | Preserve the integrated linear history. Do not refresh the shared checkout back to `b045dbf`; do not install the incomplete shelf output. Next build needs explicit handoff from `docs/ACTIVE_BOOKMARKS_CHECKPOINT.md`. |
| Folder text bug fixed by discarding more than the bug | `sidebar_tree_row_view.cc::OnPaint` now draws open/closed folders, but `Bind` no longer reads the optional folder icon. The imported `star` no longer appears as clipped `sta`; valid custom icons also disappear. | Keep the caret-free interaction. Add a small typed icon resolver with a fallback and a visible expansion state. Use one vocabulary across sidebar surfaces. Recheck long labels, narrow widths, focus, drag and contrast visually; boolean icon-state tests are insufficient. |
| Motion implementation proves only part of the promised interaction | `browser_sidebar_host_workspace_transition.cc` commits the domain switch before `Start`; it passes the whole host layer. `workspace_transition_animator.cc` resets all animations on those layers and returns without motion in reduced mode. | Distinguish committed transition from gesture preview/commit/cancel. Clip movement to owned sidebar content, retain fixed controls and cancel only owned properties. Same-WebContents suppression is sound. Dot, keyboard, gesture, interrupted transition and reduced motion still need direct evidence. |
| Performance requirement mixed response with decoration | `visual_style.h` defines a 165 ms transition while the old `PERF-04` demanded an undefined complete visible switch under 100 ms. | Define response/commit, first presented feedback and animation end separately. Preserve the sub-100-ms interaction target; animation must not block interaction. No performance pass is implied by the wording correction. |
| Checkbox fix is real; import surface still has redundant consent | Arc CSS overrode the checkbox flex host. The template also asks for both backup and commit confirmation before its Import action. | Verify alignment of all five current visible controls. Then simplify in a coherent UI package: required automatic backup as status, real categories as choices, one deliberate import action. Preserve immutable preview/backup, source safety and recovery. |
| One-click installation exposes audit implementation details | Master requires full hashes, commit and identity on the initial uBO surface. Its trust checks are useful but those values dominate the user's decision. | Present name/version/source and permission effects first; make full immutable metadata accessible in Details. Keep Chromium permission confirmation and the narrow authentic-CRX boundary. AnyChat stays on the ordinary Store flow. |
| Cmd-Q friction was accepted too quickly as Chromium behavior | User reported inability to quit; prior checkpoint attributes it to `browser.confirm_to_quit` and a held shortcut. That explains a possible cause but does not prove a successful quit or ideal default. | Specify normal short Cmd-Q for fresh profiles, retain explicit hold-to-quit preference and visible feedback. Verify menu, tap/hold as applicable, Before-Unload and restart. Actual preference change remains unimplemented. |
| Degoogle/privacy contract was ambiguous by mode | No telemetry/background-product-services appeared under `Mehr Schutz`, while fresh profiles default to maximum website compatibility. Lean config initially excludes only Compose and PDF Save to Drive. | Product-wide telemetry/account restrictions apply in both modes. Website compatibility must not re-enable product telemetry. Preserve security services and normal Google websites. Claims about size, idle activity and network need measured runtime/GN evidence. |

## Foundations worth retaining

- Chromium owns actual tabs, `WebContents`, profile isolation, extensions,
  navigation and security; Ahoi owns its product projection and policy.
- Arc's immutable source snapshot, deterministic identity, durable journal and
  explicit recovery are valuable. Do not replace the importer because a UI or
  split-reconstruction seam failed.
- The zero-tab split guard in patch `0022` targets the reported
  `NewSplitTab -> IsTabPinned(-1)` failure. The early split-feature product
  policy and removal of a test-private feature enablement close a real
  configuration gap. Source coverage still requires candidate-bound runtime.
- Classic's exact official package/key/ID boundary and normal MV3/AnyChat
  lifecycle are preferable to global MV2 enablement or a bespoke AnyChat loader.
- Local-first Sync, excluded secret domains, semantic appearance tokens,
  incremental builds and signed atomic installation remain appropriate.

## Technical follow-through

Use the package order in the master. Centralize zero-tab command eligibility
and cover each visible entry point without synthesizing a tab just to make the
empty window work. Explicit Create Split may seed a real tab as already
implemented. Keep normal profile data and runtime selection distinct from the
saved tree; scope any shared transaction to the state it actually owns.

For import and extension rollback, prove owned artifact/state cleanup and
unchanged pre-existing user data. Avoid promises that every unrelated Chromium
cache byte must be identical after an attempted network operation. The visible
result must agree with the durable transaction and recoverable state.

For tests, prefer expected user behavior, error boundaries and an authentic
product configuration. Do not add tests that only duplicate helper booleans or
assert source spelling. Do not privately enable a feature that the installed
product needs to enable itself. A red automated test must be classified as a
product defect, fixture defect or infrastructure failure before changing it.

Before runtime completion, exercise the real installed surface through normal
menus, clicks and keyboard. The Computer Use inventory is working in this
review; it reports AhoiBrowser stopped. No new Ahoi UI journey was performed.

## Current evidence boundary

- Canonical inspected branch: `codex/desktop-core-feature-wave-20260830`.
- Installed `Info.plist` currently reports source `1f5f22fbfe26069572a2861ecaf7304a25f82a54`.
  The existing matching install receipt is
  `artifacts/install/ahoi-dev-1f5f22f-20260904T170613Z.json`.
  This review did not recompute the entire installed bundle hash.
- `b045dbf` presentation corrections are committed and inherited by `d6cfa71`;
  they are not yet proven in the installed app.
- Bookmark/overlay work is owned by thread
  `01a06d69-1034-7372-b784-0b05a53c87e0`; Mobile remains separately owned.
- No browser build, installation, profile mutation or test pass was produced by
  this review. Documentation changes cannot be reported as fixed runtime.

## Goal-control boundary

The available `create_goal` cannot replace an unfinished goal and `update_goal`
only changes completion/blocking status. Computer Use explicitly refused access
to `com.openai.codex` for safety reasons. Do not edit internal databases, spoof
completion or open a second session writer to work around this. The new goal
text is the opening section of the revised master; the product supports goal
control via `/goal` ([official documentation](https://learn.chatgpt.com/use-cases/follow-goals)).
