# Combined Desktop build — terminal failure

Source: `dc01cb52a3c43c7358ece803985624aa4dc92624`, clean detached snapshot
`/private/tmp/ahoi-desktop-package1.5g65WO/repo`. Includes the explicitly handed-off
Bookmark freeze `c28ec4af0d46c0d1b62a8ea67d95cf5a0c05aff8`, Desktop Arc/Sidebar
corrections through `6bd3b70`, and test-only native target-policy preparation.
No global v3 writer or native Tree schema migration was activated.

## Executed phases

- The earlier FillIt Blender data-bake CPU gate was observed and respected.
  Once it ended, all-project CPU was rechecked before overlay and build. No
  foreign process was stopped, paused or reprioritized.
- Guarded overlay session `62030`: **Exit 0**, checkout refreshed and complete
  delta verified. Start recorded at 2026-09-05 13:24:26 UTC.
- Guarded combined Dev build session `90068`: started 13:28:24 UTC; terminal
  **Exit 1** confirmed at 13:57:30 UTC. `AHOI_NINJA_KEEP_GOING=1` collected
  independent failures in one Ninja graph. No per-error restart occurred.
- Host/toolchain checks and pinned hooks passed: Apple M2 Max/32 GiB, macOS
  26.6 (25G72), Xcode 26.6 (17F113), macOS SDK 26.5 (25F70), compatible-development
  provenance. Initial free space was approximately 86.2 GiB, above 64 GiB.
- Chromium remains `152.0.7977.65` / `fc4d67f1788019a27e32511137ceccbd2fafdaaa`.
  This was not a Chromium roll, upstream control or release build.

The existing guarded script built `chrome` plus:

```text
ahoi_tab_tree_unittests
ahoi_session_unittests
ahoi_startup_policy_unittests
ahoi_arc_import_unittests
ahoi_arc_import_browsertests
ahoi_sidebar_tree_unittests
ahoi_extension_policy_unittests
ahoi_extension_ui_unittests
ahoi_ubo_browsertests
ahoi_navigation_surface_state_unittests
ahoi_floating_browser_view_browsertests
ahoi_sync_unittests
browser_tests
interactive_ui_tests
```

These were requested targets, not a list of successful executables. Independent
objects and some link steps completed, but the combined graph is red. No new
successful build/signing receipt, test execution or installation occurred.
Outputs in `out/AhoiDev` are partial and must not be treated as a runnable
candidate or validated with a previous successful receipt.

## Complete primary compile causes

| Source location in the frozen candidate | Cause | Correction ownership/status |
| --- | --- | --- |
| `session/shared_tab_target_policy.cc:56` | `GURL::scheme()` returns `std::string_view`; implicit conversion to `std::string` is rejected | Desktop explicit-copy fix committed as `1ea90da`; not compiled in this run |
| `sync/bookmark_sync_bridge_types.cc:53` | Dynamic indexing of the `"89ab"` C-array is rejected by unsafe-buffer analysis | Bookmark owner; bounded safe-container/arithmetic correction requested |
| `sync/cloudkit_sync_provider_mac_consent.mm:88` | `BindOnce` receives a capturing lambda; state must be passed as bound arguments | Bookmark owner; the bind-internal assertion is the same cause, not an extra defect |
| `sync/profile_sync_service.h:248` | Bookmark WeakPtrFactory is before ordinary members | Bookmark owner; move into the terminal factory group without altering invalidation semantics |

The header issue caused numerous downstream failures; they are not independent
root causes. Full feedback/ownership request is queued in
`01a071df-73b7-7f33-8bbe-cc062bde2ae9`. Common files remain with their owner;
no in-place checkout edit was made. A proposed patch, if present here, is only
an unapplied review artifact until the owner corrects or explicitly hands off.

The minimal three-file proposal is now retained as
`bookmark-compile-fixes.proposed.patch`, SHA-256
`7085d79fe1af2b458f98450823b598c77809f1ac4280aac48bf6f80aa92ff073`.
Main reviewed the hunks and independently ran `git apply --check` successfully.
It is NOT applied; the three product files are unchanged. UUID bytes,
cancellation-generation handling and separate weak-pointer domains are retained;
there is no warning suppression or unrelated consent-policy change.

The separate live-account/consent/projection race remains open and is not fixed
or accepted by this compile run. Provider upload/decryption protection and local
projection authorization are distinct; no observed network leak is claimed.

## Restoration and installed candidate

After failure, both temporary dependency workarounds matched their pinned
original bytes and path-scoped Git diffs were empty:

- Chromium `build/rust/gni_impl/rustc_wrapper.py`:
  `3ddaae81891ab9397a734bbaffffd69bcca65e90c418aaa7cd3995eeccf4bbe5`.
- V8 `third_party/inspector_protocol/code_generator.py`:
  `2d6b7a3f3bcb1becadc1bc9518d1016cd5d9eee221c8180c49c54350002ad619`.
- No new `ahoi-dependency-build-workarounds.json` success receipt was published.

Installed plist remains `3d413efb5b6f196403e92f51631c346c9c55b2e5`; its executable
SHA-256 is unchanged:
`ab4d0a7664fb8ec871391be1130ee002e79ef8bc4084ff49877d4042e387aa99`.
It had been quit normally before build. No installed-bundle or profile mutation
was performed by this build. The Arc recovery journal is still unresolved.

## Retained logs and next gate

- `overlay.log` SHA-256:
  `c02f07e0d2362ab76bec76dbf852783c68915a0fc0f8a41d5a78170dc4f701cb`.
- `build.log` SHA-256:
  `a7aa37fdaaf96f5ee96e343ed5521e6e9337bc5f33c7745a1b876db257074e0b`.

Reuse incremental outputs after the four causes are corrected in one approved
committed package and the clean snapshot is advanced. Recheck CPU/disk before
refresh/rebuild: at terminal readback a different FillIt Blender source-03 data
bake, PID `57077`, was active well above 80%. Historical 12:34 Unity/PID37773
reports do not establish this current gate. Actual `.83` roll remains separate.
Visible E2E on the exact corrected candidate precedes focused programmatic
acceptance; no build/link/source review can substitute for it.
