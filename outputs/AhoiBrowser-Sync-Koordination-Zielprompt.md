# AhoiBrowser: unified sync integration and coordination

Authorized by the user on 2026-09-05. Continue the existing implementations on
`codex/desktop-core-feature-wave-20260830`; do not restart or create competing
branches, writers, transports or release paths.

User priority clarification: working product first. Fix controllable startup/
journey failures and finish integration, build the runnable app, then perform a
short representative real E2E. Only afterward run the smallest meaningful
programmatic checks. Do not delay that flow for additional fixtures, coverage,
assertion audits or unrelated test binaries. Existing safety/provenance guards
remain. Optional test-review work is deferred while the app/flow is still broken.
Real external/hardware E2E gates permit bounded independent checks, not a
prolonged test-only substitute for implementation.

## Objective

Finish one coherent, active sync wire model for Desktop and Mobile, with matching
C++ and Swift behavior and exact-candidate cross-client acceptance. The agreed
target is model v3. The user's explicit clarification in the Sync thread at
2026-09-05 16:42 UTC says the app is not actively used and a complex data migration
is unnecessary. Use fresh isolated acceptance stores/collections; do not retain
parallel active writers or build a permanent legacy-compatibility product.
Preserve existing profiles and CloudKit data unchanged outside those test stores;
fresh acceptance is not permission to reset/delete old data or reuse old keys.

Wire-model versions are separate from SQLite schema versions, encryption-key
versions, app build numbers and extension Manifest versions. Do not change those
unrelated numbers merely to make them identical.

Use ADR 0009 / `config/sync-format.json` for the unified model and binding
[ADR 0010](../docs/decisions/0010-full-browser-setup-sync.md), published in
`79d21020755c9eed78176ded6dd14ddf81a094ce`, for full browser-setup restoration.
Retain ADR 0006/0007/0008 domain/privacy rules only where not superseded.
The Sync owner extends the same current-format C++/Swift contract and canonical
golden before activating additional consumers; neither the original 13 classes
nor their 26 examples cap the authorized product scope. Preserve
consent/account/key isolation, local-only private/cookie/login state, stable
logical TreeNode IDs, separate presence/runtime IDs, immutable TreeNode creation
time and honest creation provenance. A writer-constant bump is not integration.

## Authorized scope additions — 2026-09-05 20:08/20:12 UTC

The user's direct instructions in both owner sessions, confirmed to the
coordinator, extend the product scope without changing ownership or reopening
the current startup-fix candidate:

- Desktop implements local workspace-specific cookies/site sessions after its
  current package. History, passwords and extensions remain global by default;
  workspace-specific toolbar pins may be added. Use an explicit native
  BrowserContext/storage design, not custom credential handling.
- Sync implements transferable native Chrome user preferences, actual trusted
  extension installation/enabled state and positively reviewed extension-owned
  settings. Five Ahoi preferences plus an inventory are explicitly insufficient.
  Publish the supported/excluded settings catalogue; use real native user
  preferences, not copied effective managed/extension-controlled values.
  Distinguish installed, pending, blocked, confirmation-required and failed.
  Preserve native permission prompts, source/signature checks and MV2/uBO limits.
- Extension settings require reviewed ID/key/value schemas or an equally
  explicit safe contract. `storage.sync` alone is no secret-free guarantee;
  unknown opaque data and raw local/session/managed stores remain local.
  Mobile applies supported mappings and retains recognized desktop-only metadata
  without claiming Chromium extension execution.
- Cookies, passwords, login tokens, secrets and machine-local paths remain
  excluded. Do not copy whole profiles, Preferences files or extension stores;
  use native restoration and trusted extension sources with explicit eligibility.
- The original 13-class manifest is a baseline, not a prohibition on this newly
  authorized scope. The Sync owner evolves the single shared contract where
  necessary and coordinates native settings/workspace metadata with Desktop.
  New upstream/native install, preference and storage hooks require concrete
  file-level handoffs; do not take over Google's sync processors concurrently.

Finish working integration and representative E2E, not a new exhaustive test
matrix. Existing runtime, CPU, privacy and non-destructive boundaries remain.

## Roles

- Sync implementation/integration owner: `01a06d69-1034-7372-b784-0b05a53c87e0`.
  Owns its existing common C++ model/codec/merge/store/provider/config/GN/Golden
  and Bookmark adapter scope, plus the explicitly handed-over Swift sync/model/
  persistence/Mobile binding/projection/test work in `spikes/cloudkit` and
  `apps/AhoiMobile`. No further Swift ownership request is necessary.
- Desktop owner: `01a04f97-e3ba-70f2-a031-220b214d352d`. Continues the current
  Desktop browser package. Remains sole owner of Chromium checkout/out, guarded
  build, signing, installation and native UI. Native Tree/Session/UI and
  `tab_tree_sync_adapter.{h,cc}` remain here unless an exact disjoint handoff
  is explicitly agreed. The Sync owner supplies concrete integration needs.
- Coordinator: `01a044d6-1545-7532-8394-6b7df1144bb1`, formerly the Mobile
  implementation owner. Owns this prompt and `docs/ACTIVE_SYNC_COORDINATION.md`,
  coordination messages, read-only reviews and evidence assessment. Does not
  keep editing the handed-over product files or start a third sync implementation.

No new helper is needed just to duplicate these existing owners. The current
native frozen build must finish without being widened/restarted by this handoff.
Fresh cross-project CPU gates and the explicit native runtime handoff rules
continue to apply. Never interrupt another owner's build.

## Execution

1. Obtain one explicit role acceptance from each owner. Resolve concrete file
   overlaps once; do not recreate field/ownership questions already frozen.
2. Sync owner produces a short bounded package sequence: remaining matching
   C++/Swift implementation, fresh isolated bootstrap, native/Mobile bindings,
   activation and acceptance. Existing source and tests are reused.
3. Preserve the already committed Swift source fixes `895daf9` and `f25eea5`
   and their seven unexecuted provenance regressions as source evidence. They
   are not test passes. If retired legacy code makes a case obsolete, document
   that precisely rather than perpetuating the compatibility path for its test.
4. Owners send only material progress, conflicts, source freezes and terminal
   build/runtime handoffs. Coordinator verifies the actual relevant files,
   commits, logs, receipts and test counts rather than treating messages or
   process absence as successful execution or a lease transfer.
5. Integrate coherent candidates with one build owner. Perform representative
   visible E2E before focused programmatic tests. If E2E is genuinely unavailable,
   document the exact boundary and run meaningful independent tests; do not
   claim E2E acceptance. Do not repeat unchanged Build15 Bookmark acceptance.

## Acceptance

Matching exact Desktop and Mobile candidates must implement the same active
wire model and demonstrate a real representative sync journey: the shared
Bookmark collection remains separate from Workspace Saved Pages; normal tabs
appear on an already-open peer without stealing focus or eagerly loading;
save/unsave preserves logical identity; temporary and persistent behavior and
local-only targets remain coherent. Exercise persistence/restart and a bounded
edit/delete roundtrip, followed by focused version-rejection, provenance, consent,
conflict/no-echo and private-data exclusion regressions.

ADR 0010 additionally requires a configured Mac A -> fresh install/link Mac B
journey with native settings actually applied, a supported extension installed
and usable, and a supported extension setting visibly converging. Exercise
representative reverse edits, already-open peers, restart/offline and genuine
disable/uninstall/default-reset intent without echo or unwanted restoration.
Fresh defaults or an empty inventory are not deletion intent. Show unsupported
and consent-required states honestly, including iOS's supported mappings.
This is working-product acceptance, not another exhaustive test matrix.

Keep source, compiled tests, installed app, real Development transport, simulated
peer, native Chromium/mobile roundtrip, cross-device key bootstrap and Production
acceptance separate. A simulated Swift peer cannot satisfy native Chromium proof.

No Production rollout, new portal/account/provisioning action, existing key
mutation, profile destruction or destructive migration is authorized by this
coordination assignment. Exhaust safe in-scope work; report a precise additional
authority/external gate if needed. Do not invent success to close the goal.
