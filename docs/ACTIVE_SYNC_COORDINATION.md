# Active sync coordination

Updated: 2026-09-05. Coordinator: `01a044d6-1545-7532-8394-6b7df1144bb1`.
Registered goal: joint sync-model integration and candidate-bound acceptance.
Contract: `outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md` plus ADR 0009,
retaining the domain/privacy contracts of ADR 0006/7/8 where not superseded.

## Assignment and current acknowledgments

The user explicitly approved one unified Sync implementation owner while the
other existing Terminal agent continues its Desktop package, on the same branch.
Additional direct user messages in the Sync session at 16:35/16:42 UTC request
one format across relevant data and explicitly avoid complex migration because
the app is not actively used. Fresh isolated acceptance stores are authorized;
existing profiles/CloudKit data are not to be deleted. This clarification
supersedes migration/legacy-reader work in the initial coordination wording.

- Sync: `01a06d69-1034-7372-b784-0b05a53c87e0`, observed Terminal `ttys011`.
  Existing common C++ scope plus Swift `spikes/cloudkit` / `apps/AhoiMobile`
  transferred cleanly through source `f25eea5`; explicitly accepted in
  `ae740b5192ac7e64e149bc58aa64de306690e2a8`. ADR 0009 and the Sync checkpoint
  now assign both languages to this one owner. No further ownership ACK or
  separate Mobile implementer is needed. The agreed sequence is common format/
  fixtures and fresh C++ store, matching Swift, native/Mobile binding, then a
  coordinated candidate/E2E/focused-test wave. Initial handoff message was
  `01a07275-a813-73b2-aabb-40399075fa5d`; delayed notices using the old split
  do not reopen it. The unified contract remains `1bfae11`.
- Desktop: `01a04f97-e3ba-70f2-a031-220b214d352d`, observed `ttys002`.
  Browser package, native Tree/Session/UI and adapter, Chromium checkout/out,
  build/sign/install/native runtime retained. Explicit role acceptance received
  after reading committed `48b1ef4`: Desktop will not edit the unified common
  C++ or transferred Swift scopes without an exact handoff. Initial assignment
  message: `01a07276-1cbe-7942-a71c-ba76f2ddc30e`. No further role ACK needed.
- Coordinator: read-only product review/coordination, this checkpoint/prompt;
  no parallel product edits in the transferred scopes. Native integration
  changes require exact Desktop/Sync file or interface handoffs.

Queued messages are not acknowledgments. Terminal/PID observations are not leases.

## Concrete native integration boundary

Desktop explicitly accepted all native packages A-D in `59a0378`, detailed in
`docs/SHARED_TAB_NATIVE_SEAMS.md`: target-type alias, SessionBridge/TabTree
identity and persistence, adapter projection, sidebar capture/close. No native
files are delegated back. Common test files/GN need separate exact coordination.
Native A is now unblocked by the explicitly delivered `5885d01` five-file
common code handoff: shared target/capture/state types, default-false UI bridge
and isolated GN `shared_tab_types` leaf. Coordinator inspected the full manifest,
headers and dependencies; no writer/model/store changes. Desktop has the exact
code/dependency handoff for its alias; no further A format/ownership ACK needed.
B-D still await the actual service/getter/capture/backend implementation handoff;
these interfaces alone are not working native behavior. Baseline work is separate.

## Current evidence boundary

- Branch: `codex/desktop-core-feature-wave-20260830`.
- Historical `30212` / `225df88` ended EXIT 1. Its five API diagnostics were
  resolved by `5794d37` + `22e2f2b`; that exact source handoff is accepted.
- Overlay `65019` ended EXIT 0; `7945` / `22e2f2b` ended EXIT 1 (`752723f`).
  Remaining diagnostics were only raw `kLegacyRows[i]` access in one fixture;
  Sidebar tests linked, not an overall-build/test pass. Full logs/reports:
  `artifacts/build/desktop-test-api-22e2f2b-20260905/`. Never poll/restart 7945.
- **Source blocker now resolved:** `92694fe36539d024af6567646103f1cf246d5364`
  is separately committed/pushed. Coordinator verified its complete one-file
  3+/2- diff: `<array>` and `std::array<LegacyRow, 2>`; payloads and assertions
  unchanged, no warning suppression or unified-format WIP. Desktop received
  concrete continuation message `01a072e3-ba51-7293-a596-0ec6a2938f52` after its
  blocked report. The existing cached snapshot/build should continue only with
  that bounded fix and a fresh CPU gate. Desktop subsequently accepted the
  resolved blocker in `82c4074`, selected the clean `92694fe` snapshot and
  started overlay `60701` after the 18:52 UTC gate cleared. Coordinator then
  directly observed its live guarded Dev wrapper (PIDs 94538/94819) doing
  overlay verification, logging to
  `artifacts/build/desktop-fixture-92694fe-20260905/build.log`.
  Work has resumed; its later result is recorded below.
- **Build verified:** Desktop confirms `17302` TERMINAL EXIT 0 on clean
  `92694fe`, after overlay `60701` EXIT 0. Coordinator read the completion/signing
  log and `LINK ./ahoi_sync_unittests`, then verified the canonical receipt
  `artifacts/build/ahoi-dev-build-92694fe36539.json`: source `92694fe`, clean,
  overlay applied, SHA256
  `dec35aefe6a4095fa784dcd5f2cf186a8005e9ac53c5f81e4b6ed4163f3772a1`.
  This is compiled baseline evidence, not executed tests or unified-format proof.
- Desktop's guarded atomic installer is reported RUNNING `19738`, expected
  receipt `artifacts/install/ahoi-dev-92694fe-20260905T190452Z.json`.
  Installation, the new installed readback and visible E2E remain unverified;
  no coordinator UI/out access while it runs. Unified/header WIP stays excluded.
- Minor convention follow-up: the read 92694fe commit body lacked a DCO trailer.
  Sync was asked for a traceable attestation without silently rewriting published
  history (`01a072e3-bcd5-7b03-9dfb-3e114c1f26b7`). This is separate from whether
  the source compiles. The partial example queue prefix in that message is not
  evidence; the exact Desktop continuation ID is the one recorded above.
- No current CPU state is inferred from historical Blender/Unity PIDs. Each
  intensive start requires a fresh gate. No coordinator build or runtime lease.
- Installed Desktop remains `3d413ef` per current Desktop checkpoint; that
  native candidate is provider-free and is not a real cross-client sync pass.
- Swift `3964bcb` is frozen-contract preparation; `895daf9` and `f25eea5`
  correct retained local provenance. Seven provenance test methods are written,
  not executed. No Mobile build/test/host or My-Mac lease is owned here.
- Earlier `313e351` / Build15 Bookmark acceptance remains its own evidence;
  it neither covers the new source nor needs an unchanged repetition.

## Verified unified-format progress (not acceptance)

- ADR 0009 and `config/sync-format.json` are committed in `1bfae11`; Desktop
  aligned its master/native contracts in `006930f`. The manifest reads as exactly
  13 entity IDs 0–12, all authored/accepted at 3; obsolete input must not turn
  into a deletion or an authoritative empty snapshot. Its golden path is
  `overlay/chromium/src/ahoi/browser/sync/testdata/sync_wire_v3.json`, not yet
  observed as a delivered all-entity fixture.
- Actual uncommitted C++ implementation is now visible in common model/store/
  profile types and the two new shared-tab leaf headers. Coordinator inspected
  the leaf types, separate presence/logical IDs and default-false native support.
  This is WIP, not a header freeze, compiled result or writer activation.
- Swift implementation has now started too: central `SharedSyncFormat`, exact
  live class/version boundary and Capability `[3]` defaults are visible WIP.
  The coordinator's remaining-path review is
  `docs/reviews/2026-09-05-unified-sync-swift-checklist.md`; it is source-only,
  not a claim that unfinished codecs, snapshot markers or live bindings pass.
- Direct Terminal Computer Use was denied by the tool's safety boundary. No
  workaround or process interruption was attempted. Coordination continues via
  the existing CLI queue and scoped read-only process/log/checkpoint evidence;
  a queued message is still not an immediate steering or acceptance proof.

## Next actions

1. Both owners have accepted their roles. Follow the concrete package sequence
   and native header handoffs; no further role-confirmation loop.
2. Track the next bounded native correction handoff/build without starting
   competing intensive work. Source, build, tests, signing and install
   remain separate gates.
3. Sync owner closes matching C++/Swift, fresh bootstrap and live binding
   gaps, using exact native adapter handoffs where needed; target one active
   model, not perpetual v2/v3 writer coexistence or complex old-data migration.
4. Agree the next coherent source freeze and candidate-bound verification.
   A native Development-capable app/profile/key-bootstrap and a real roundtrip
   are still unproven; no account/portal/Production authority is implied.

Only update this checkpoint on meaningful state changes. Preserve detailed
historical build/test evidence in the existing reports, not repeated ACK logs.
