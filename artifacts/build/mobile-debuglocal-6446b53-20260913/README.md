# Mobile Home, Reader, Structure and Private Lock candidate — DebugLocal25

Exact clean source `6446b534b3269befaf36a07fb80bde6e0251e745`
built successfully as the normal product-only arm64 iOS Simulator target. The
source includes the committed format-3 Structure/Home metadata block
(`e7abcff`), private-session lock (`9ca3bd1`) and normal Home/Reader/Markdown
actions (`8e81cfc`). Canonical foreign Native/Chromium WIP was excluded by a
detached worktree.

## Candidate binding

- Configuration/version: `DebugLocal` 0.1 (25)
- Bundle/platform: `app.ahoibrowser.AhoiBrowser`, `iphonesimulator`, arm64,
  minimum iOS 26.0
- Build: terminal `EXIT 0`, `** BUILD SUCCEEDED **`, Xcode 26.6 / 17F113,
  generic iOS Simulator destination, `-jobs 2`, Swift `-j2`
- Product graph: four product/dependency targets only; no tests or test targets
- App: `AhoiMobile-6446b53.app`
- Build log/result/exit: `build.log`, `build.xcresult`, `build.exit`
- Clean source snapshot/DerivedData:
  `/private/tmp/ahoi-mobile-debuglocal25.7q8d3O/repo` and
  `/private/tmp/ahoi-mobile-debuglocal25.7q8d3O/DerivedData`
- Compact source delta: `source.bundle`; it contains `6446b53` and declares
  `fa53e31` as its prerequisite

The existing `mobile_release_candidate_receipt` tool created and then verified
`candidate.json` against the clean detached source, generated Xcode project and
preserved app. It records:

- app tree: `2bd065664f717ace4641ac16a61f1ea9e937d3845e662e816067771299cad31e`
- executable: `eee77caeaec1d0f9079fc6fde41aaa5a754814cacaf4ea5c598d8ace2b1d0846`
- Info.plist: `78852b558f2b093618c3be59da7334f53e06e1ecba2c8d0a03bc001e0fb5a3df`
- Xcode project: `217998e07409605422754f10c0da9de7691f855b070b577a2de59b1b6bb6ed53`
- ad-hoc simulator signature: valid, deep/strict verification passed,
  distribution eligible: false

DebugLocal remains provider-free: the built CloudKit container, Sync/command
Keychain-group and key-version values are empty. The configured subscription
and zone labels alone cannot construct the guarded provider without the empty
container/key inputs. The separate Structure-Development scope committed later
in `2fd0321` was not applied to this build; it is reserved for the future
entitled schema7/structureRevision1 partner.

## Capacity and remaining gates

The first sample had 55–62% CPU idle but active swap movement and a foreign
Xcode UI run, so no build started. The second sample had about 81% CPU idle,
stable swapouts and no Xcode/Swift/compiler process; the two-job build then ran
to its natural completion without touching the remaining low-CPU Docker job.

No Simulator was booted and the app was not installed or launched. No CUA,
Device24, Keychain, CloudKit, Portal, profile, provisioning or signing update
occurred. This candidate is only the later local visible Home/Reader/Markdown,
Structure-preservation and private-lock UI candidate. Visible E2E, focused
regressions, device/CloudKit Sync and distribution acceptance remain open.
