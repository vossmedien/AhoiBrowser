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
  transferred cleanly through source `f25eea5`. Assignment queued as
  `01a07275-a813-73b2-aabb-40399075fa5d`; acceptance/short package plan pending.
  A subsequently delivered format notice still used the old split (Swift here,
  common C++ there). Coordinator clarified the newer user-approved C++/Swift
  ownership in `01a0728d-7fd1-7df3-8c1d-b130414c6847`; no product writes resumed
  here. Format 3 for every existing permitted entity, including Bookmark and
  Capability, is now committed as ADR 0009 / `config/sync-format.json` in
  `1bfae11`. Its Common/Mobile role labels and the Sync checkpoint still use
  the old split; the queued newer ownership clarification remains to be accepted.
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

## Current evidence boundary

- Branch: `codex/desktop-core-feature-wave-20260830`.
- Native correction build: Desktop's updated checkpoint confirms Session
  `30212` TERMINAL EXIT 1, clean `225df88`, Chromium `.65`, after successful
  overlay `54118`. Coordinator read the Ninja terminal failure: two string
  argument errors and two Database constructor diagnostics in three SQL test
  files, plus protected animation-container access in one sidebar test.
  Browser/large browser-test linking is not full-build success or installation.
- Corrective source now exists as `5794d37` (Desktop animation test) and
  `22e2f2b` (three SQL test files). Exact subsequent freeze/build handoff belongs
  to Desktop; no unchanged restart or new candidate is inferred. Coordinator
  reviewed the exact three-file `22e2f2b7a3f5b0832cc7eff3d23819a6041aa737`
  manifest. Desktop has since explicitly accepted the direct sender handoff in
  its checkpoint / 17:27 UTC readback. No SQL-source wait remains. Desktop's
  clean cached build snapshot is now `22e2f2b`, including `5794d37`, not the later
  unified-format WIP. Do not repeat or recreate this handoff.
- Canonical log: `artifacts/build/desktop-correction-225df88-20260905/build.log`.
- Desktop explicitly confirms terminal `30212` at 16:45:34 UTC, report `813adb8`,
  no current Ahoi compiler and no new install. Its next intensive start was
  blocked by identified FillIt Blender PID `93670`, measured 214.5/302.3/144.1%
  CPU. These are Desktop's point-in-time measurements; every new phase needs
  its own fresh gate. The message grants no My-Mac/native UI or shared-out lease.
- Later Desktop readback (17:31 UTC) reports the Blender job ended and a new
  FillIt Unity phase around 191% CPU blocking the start. Coordinator observed
  Unity PID 25581 / parent 51136 at 188.6%; no old PID is a permanent blocker.
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
- Direct Terminal Computer Use was denied by the tool's safety boundary. No
  workaround or process interruption was attempted. Coordination continues via
  the existing CLI queue and scoped read-only process/log/checkpoint evidence;
  a queued message is still not an immediate steering or acceptance proof.

## Next actions

1. Obtain the remaining Sync-owner acceptance and short package plan; Desktop
   has explicitly accepted its retained role. Do not repeat the Desktop ACK.
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
