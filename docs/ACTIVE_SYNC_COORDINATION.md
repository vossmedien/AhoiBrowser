# Active sync coordination

Updated: 2026-09-05. Coordinator: `01a044d6-1545-7532-8394-6b7df1144bb1`.
Registered goal: joint sync-model integration and candidate-bound acceptance.
Contract: `outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md` plus ADR 0006/7/8.

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
  Capability, agrees with the unified target; exact updated ADR/Golden is pending.
- Desktop: `01a04f97-e3ba-70f2-a031-220b214d352d`, observed `ttys002`.
  Browser package, native Tree/Session/UI and adapter, Chromium checkout/out,
  build/sign/install/native runtime retained. Notification queued as
  `01a07276-1cbe-7942-a71c-ba76f2ddc30e`; role confirmation pending.
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
  manifest and requested the missing direct Sync freeze confirmation in
  `01a0728a-90fe-7b62-9063-952b294b1fc7`, notifying Desktop separately. This
  bounded test-API handoff must not wait for the larger unified-format package.
- Canonical log: `artifacts/build/desktop-correction-225df88-20260905/build.log`.
- Installed Desktop remains `3d413ef` per current Desktop checkpoint; that
  native candidate is provider-free and is not a real cross-client sync pass.
- Swift `3964bcb` is frozen-contract preparation; `895daf9` and `f25eea5`
  correct retained local provenance. Seven provenance test methods are written,
  not executed. No Mobile build/test/host or My-Mac lease is owned here.
- Earlier `313e351` / Build15 Bookmark acceptance remains its own evidence;
  it neither covers the new source nor needs an unchanged repetition.

## Next actions

1. Read the two owners' explicit assignment acceptances and the Sync package plan.
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
