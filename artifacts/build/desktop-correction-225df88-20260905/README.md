# Combined correction build 225df88 — terminal test compile failure

Exact clean detached source: `225df88f4e6dbe692390f52b8687b653541723ac` in the
existing `/private/tmp/ahoi-desktop-package1.5g65WO/repo` snapshot. Chromium
remains `152.0.7977.65`, commit `fc4d67f1788019a27e32511137ceccbd2fafdaaa`.
This source includes the original Bookmark package, all four preceding compiler
corrections (1ea90da + 3035529), and the separately handed-over effective-consent
correction. No WIP, v3 writer, new schema or Chromium roll was integrated.

## Execution and boundary

- Guarded overlay session `54118`: EXIT 0, full checkout delta verified.
- Guarded combined Dev build session `30212`: log created 16:18:49 UTC;
  terminal EXIT 1 confirmed 2026-09-05 16:45:34 UTC. `-k 0` collected independent
  errors without stopping or mutating the running source.
- Fresh start gate: no competing build tree above 80%; 89,299,708 KiB free.
  Existing Ahoi app was closed; no foreign process was changed.
- Same targets as the preceding combined build: chrome, ahoi_tab_tree_unittests,
  ahoi_session_unittests, ahoi_startup_policy_unittests, ahoi_arc_import_unittests,
  ahoi_arc_import_browsertests, ahoi_sidebar_tree_unittests,
  ahoi_extension_policy_unittests, ahoi_extension_ui_unittests,
  ahoi_ubo_browsertests, ahoi_navigation_surface_state_unittests,
  ahoi_floating_browser_view_browsertests, ahoi_sync_unittests, browser_tests,
  interactive_ui_tests.
- The product objects, including native Bookmark adapter, backend, provider,
  Mac consent and SyncPump, compiled. libchrome_dll, browser_tests and
  interactive_ui_tests linked. The complete build did NOT succeed: no new
  successful build receipt, runtime staging/signing, installation or test
  execution is asserted. Partial output is not an accepted candidate.

## All five diagnostics — four test files

Paths below are relative to `overlay/chromium/src/ahoi/browser/`.

| File at frozen source | Diagnostic | Correction ownership |
| --- | --- | --- |
| sync/bookmark_sync_store_unittest.cc:65 | const char pointer cannot implicitly construct base::cstring_view | Bookmark owner, bounded test-only handoff requested |
| sync/bookmark_sync_authorization_unittest.cc:202 | Same pointer conversion in the four-query loop | Bookmark owner, bounded test-only handoff requested |
| sync/profile_sync_service_unittest.cc:107 and :140 | sql::Database has no default constructor in M152 | Bookmark owner; existing sql::test::kTestTag API |
| ui/sidebar/sidebar_tree_view_interaction_unittest.cc:43 | Animation::container() is protected | Desktop correction committed separately in 5794d37 |

The Sidebar correction binds a retained, independent height test container via
public SetContainer before model/widget animation begins. Both independent
containers advance once per requested frame. A narrow review rejected sharing
the BoundsAnimator container because it changes frame/completion observer
boundaries and late rebinding resets the animation start time. Production
animation code and all assertions are retained. Chromium-pinned formatting and
diff-check passed; no behavior test ran. The correction is NOT in this failed
snapshot. Common test files are not edited by Desktop; final handoff is required
before the next combined incremental run. Existing successful objects are kept.

Final feedback queues:
`01a07277-73a1-7ba1-b399-77d852964363` (Bookmark) and
`01a07277-73d8-7460-865f-a561edd09a1c` (Mobile).

## Restoration and installed state

The temporary dependency workarounds were restored and their path-scoped Git
diffs were empty after terminal:

- build/rust/gni_impl/rustc_wrapper.py:
  `3ddaae81891ab9397a734bbaffffd69bcca65e90c418aaa7cd3995eeccf4bbe5`
- v8/third_party/inspector_protocol/code_generator.py:
  `2d6b7a3f3bcb1becadc1bc9518d1016cd5d9eee221c8180c49c54350002ad619`

Installed Info.plist still reports AhoiSourceCommit
`3d413efb5b6f196403e92f51631c346c9c55b2e5`. Executable SHA-256 remains
`ab4d0a7664fb8ec871391be1130ee002e79ef8bc4084ff49877d4042e387aa99`.
No app/profile action or Arc journal/backup mutation was performed by this run.

## Canonical evidence hashes

- overlay.log: `c02f07e0d2362ab76bec76dbf852783c68915a0fc0f8a41d5a78170dc4f701cb`
- build.log: `56e901d877212f6bb4c4f8c5f7d0bbece39eb5a2b96c4935c6500a4b5bb929a9`

These logs and this report are failure evidence, not test or release evidence.
