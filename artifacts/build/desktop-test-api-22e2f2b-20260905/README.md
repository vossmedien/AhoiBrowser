# 22e2f2b incremental correction — terminal fixture compile failure

Exact clean source: `22e2f2b7a3f5b0832cc7eff3d23819a6041aa737`; existing detached
snapshot `/private/tmp/ahoi-desktop-package1.5g65WO/repo`, shared out/AhoiDev.
Chromium remains 152.0.7977.65 / fc4d67f1788019a27e32511137ceccbd2fafdaaa.
Only the four agreed test files differ from 225df88 in the Desktop build inputs.
No unified-format code, default, policy or schema was integrated.

- Guarded overlay session65019: EXIT0, checkout-refreshed and delta verified.
- Combined guarded Dev build7945: started after fresh quiet gate at17:59UTC;
  TERMINAL EXIT1 confirmed 2026-09-05 18:08:43UTC. No in-place source update.
- Same complete target set as the previous combined build, including
  ahoi_sync_unittests, ahoi_sidebar_tree_unittests, browser_tests and
  interactive_ui_tests; existing successful objects reused.
- Ninja needed seven actions. The corrected profile/service authorization and
  Sidebar interaction test files compiled; ahoi_sidebar_tree_unittests linked.
  All five preceding API diagnostics are gone.
- One remaining root cause: raw constexpr `kLegacyRows[i]` access in
  `bookmark_sync_store_unittest.cc:435/436`, diagnosed by the unsafe-buffer
  plugin now that C++ type checking succeeds. The two diagnostics are one
  fixture-container issue. No safety-warning suppression or assertion change.
- Bounded preserving container/span correction or exact hunk ownership was
  requested from the Sync file owner in 01a072c4-5524-7270-9d9f-526ba9906062.
  Desktop did not edit that common test file. This is maintenance of the frozen
  UI/compile baseline, not a new legacy-migration project or format-3 gate.

No new successful total-build receipt, staging/signing, installation or test
execution occurred. A linked test executable is not a passing test or candidate.
Installed Info.plist still reports source3d413efb5b6f196403e92f51631c346c9c55b2e5.
Arc/profile state was not touched. The actual new unified-format acceptance
remains separate and uses fresh isolated matching stores/candidates.

Both temporary dependency workarounds were restored; path-scoped Git diffs empty:

- build/rust/gni_impl/rustc_wrapper.py:
  3ddaae81891ab9397a734bbaffffd69bcca65e90c418aaa7cd3995eeccf4bbe5
- v8/third_party/inspector_protocol/code_generator.py:
  2d6b7a3f3bcb1becadc1bc9518d1016cd5d9eee221c8180c49c54350002ad619

Canonical SHA-256 evidence:

- overlay.log: c02f07e0d2362ab76bec76dbf852783c68915a0fc0f8a41d5a78170dc4f701cb
- build.log: 16a0f3d6c63ece1d303e200f15a3f02d41aa939371142222b948d3f35abf8329
