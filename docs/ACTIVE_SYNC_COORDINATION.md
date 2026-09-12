# Active sync coordination

Updated: 2026-09-12. Root coordinates the same canonical branch,
`codex/desktop-core-feature-wave-20260830`. The user explicitly confirmed both
former Terminal agents stopped and authorized restarting the two workstreams.
Their old thread IDs and September8 leases are not current runtime grants.

## Current owners

| Live owner | Exclusive product scope |
| --- | --- |
| `/root/desktop_resume` | Native Tree/Session/UI, tab-tree adapter, native extension hooks/restore, Mac signing tools, shared Chromium checkout/out, guarded Mac build/sign/install |
| `/root/sync_mobile_resume` | Common C++ sync/model/codec/store/provider/config/GN/golden, Bookmark domain adapter, Swift/Mobile, iOS Development signing |
| `/root` | Coordination and acceptance, this checkpoint; no competing product implementation |

Both workers are directly addressable through the current collaboration tools.
No old `codex queue` acknowledgement is needed. Existing uncommitted September12
product-contract edits are preserved, not silently included in unrelated commits.
Global/project AGENTS apply directly.

## Binding scope and packet order

Use the relevant current sections of
[the coordinator prompt](../outputs/AhoiBrowser-Sync-Koordination-Zielprompt.md),
[ADR0009](decisions/0009-unified-prelaunch-sync-format.md),
[ADR0010](decisions/0010-full-browser-setup-sync.md) and
[the master](../outputs/AhoiBrowser-Master-Zielprompt.md), including their
September12 user changes. Old wording in the registered goal does not reopen
v2/mixed-writer or elaborate legacy migration work.

1. Close real settings/extension consumers and shared-key setup; prepare matching
   entitled candidates, not another build with an unsupported native install/
   enable placeholder. Preserve native consent and original authorization
   through commit/readback/ACK.
2. Implement the binding September12 additions in the SAME format3: logical
   Desktop split groups, non-destructive automatic archive/restore, saved-page
   Home URL distinct from current URL, and typed routing/shortcut/archive prefs.
   Common publishes exact maps/IDs and coordinates Native adapters. Mobile
   preserves recognized metadata losslessly; Mobile Split View UI is not required.
3. Integrate coherent candidates; exercise short representative real journeys,
   then needed focused checks. Signing or a simulated peer cannot close the real
   Mac–iOS / Desktop-pair roundtrip requirement.

Private state, secrets, raw extension storage, cookies/login/site data and local
runtime handles remain excluded. Received state must not steal focus, navigate
or force-close active peer pages or change their website-account context.
Fresh acceptance does not authorize deleting or resetting existing data/keys.

The full Crest-derived product additions also remain binding, not just their
sync fields: routing/remembered targets, Home/Peek, shortcuts/MRU, Reader/Markdown,
import preview/portable export, developer context/help/status and Mobile privacy
lock. Master WORKFLOW-01..08 and the matching registry IDs are present. Both
workers received the complete platform-specific follow-up scope; documentation
or an optional UI switch is not implementation/acceptance.

## Actual progress on September12

- **Native storage request hook delivered:** `4ee694e` adds patch0040 after0036.
  Root verified commit, series entry and actual request-observer declarations.
  Common consumed the direct Source handoff; the old48-line permission wait is
  CLOSED. No build/runtime pass is inferred from patch composition.
- **Native follow-up in progress:** Desktop is closing actual trusted install/
  enable/disable/uninstall plus0039, category/retry UI and the bound Development
  Prepare/Verify/Install configuration. Inventory `cf56c04` alone was not restore.
  Native/Signing source block a58e84c and bounded subsequent compiler/start-scope
  corrections are delivered. The current frozen candidate/handle and exact
  terminal diagnostics belong in [the Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md),
  not duplicated stale RUNNING instructions here. Root observed the first real
  wrapper/log; that failed first run must not be resumed as the current candidate.
- **iOS Development22 signed:** source `9658f945d7c0a80b6b5b331d6fecb4be3e40bb10`,
  iPhoneOS/arm64. Separate signed copy and receipt are under
  `artifacts/build/mobile-development-9658f94-20260908/`.
  Receipt: `development-signing-receipt-20260912.json`, evidence `2c15b97`.
  Signed tree `c28c15105060d9004433e5d91881fc9a694e928ed71e64e1e8ec3e58053953c5`.
  Root independently verified deep/strict signature, unchanged unsigned/signed
  Info.plist bytes and signed executable SHA
  `d6215784a3be8b32aeec87d564894f89395f936683b170c892471a7f5792b8d8`.
  Unsigned original remains preserved. No install, app start or CloudKit pass.
- **Locally isolated Device23 is built and signed:** fa53e31 binds domain files,
  defaults/identity, normal WebKit storage and downloads to the validated
  Development namespace. Build67411 completed EXIT0. Receipt f4c54a7 under
  `artifacts/build/mobile-development-fa53e31-20260912/candidate.json`; signed tree
  65dcd44bc99178a7baec16d84fa401c3f79ee3de93d5dab35465ffd47ed0e1df.
  Root read the receipt/actual plist and independently verified its deep/strict
  signature. Original22 and unsigned23 remain preserved. Runtime namespace
  readback, app installation and CloudKit are NOT_RUN, not signature failures.
  Device23 was installed headlessly with no Ahoi process before/after, but must
  NOT be started: three UI AppStorage properties still used standard defaults.
  Pure backport7e19476 fixes those exact bindings. Device24 e2faf54 is the clean
  fa53e31+7e19476 correction, without e7/Privacy additions; its actual build/sign/
  install result belongs in the Mobile checkpoint/receipt. This correction is
  required before the first ordinary UI/opt-in, not another feature-only rebuild.
- **DDI boundary cleared by actual user unlock:** readback20029 at10:55:57UTC
  found Servusla's DDI17F113 mounted, compatible and usable. No Xcode roll was
  needed. A later lock does not invalidate that proof or authorize blind retries;
  read the actual state at the next device action. No passcode was requested.
- **Corrected Device24 installed and ordinarily launched:** e2faf54 build54890
  and headless install8681 completed EXIT0. The signed tree is
  281f5c8b137973cb3624c894c1ea3ab6db5d23fe10d3ca5850c283b45dd16a88.
  Normal launch at11:28:36UTC used no fixture or harness arguments. Scoped local
  session/snapshot/defaults readback confirms the intended namespace and Sync
  remains OFF. This is not a visible iPhone UI or CloudKit pass. Device22/23 above
  are preserved predecessor evidence, not candidates to restart.
- **Scoped Mac installed and independently verified:** guarded install64142
  completed EXIT0 on c8d9161057cae20913ddd83bfbfadf22598d3950. Root verified the
  installed deep/strict signature, actual Development/container/zone/subscription/
  account values and executable SHA
  29118f340a5dbaceaee1c9164353bca6d9b8d9c57dc0ee17242df7b9e7402ee5.
  The real Default/Local State/tree are preserved; a24 remains in atomic rollback.
  Installation itself did not start the app. Subsequent ordinary MacA startup
  and the first real visible Sync opt-in are recorded below. Acceptance startup
  is protected to its dedicated MacA/MacB profiles, not the real Default.
- **Structure source freeze e7abcff is separate:** record13/14, Home/archive
  fields, both codecs/merge/stores/provider and common golden are implemented in
  Source, not runtime-accepted. Native DTO/persistence/capture/scheduler/UI remain
  the following integration. Its schema7/structureRevision1 is NOT in c8/Device24.
  Use a new jointly bound fresh scope if the baseline populates the earlier one;
  do not migrate or empty existing baseline data to claim a fresh next wave.

## Exact isolated Development configuration

[The shared non-secret scope](../artifacts/e2e/shared-sync-development-scope-20260908.json)
has SHA `851600c142f1c289f5f878a23587eecbc9001a4e2c2e072f7c0e0f5dc13c9abe`.
It binds Development, the dedicated Ahoi container, scope UUID
`bba96b17-f044-4923-9d40-67b15014d59e`, matching acceptance zone/subscription and
payload-key account, existing service/group, key version1. Domain wire version
remains3; those numbers are unrelated. Prepared names do not prove an empty
server zone. Use actual Claim/Journal/commitment plus synchronizable Keychain,
not copied key bytes or independently provisioned peer keys.

Both actual candidates must use that same configuration and genuinely fresh
local stores before opt-in/start. iOS23 does not contain the new September12
split/archive/Home/routing/shortcut implementation; do not claim its acceptance.
See [key setup](KEY_BOOTSTRAP_SYNC.md) and the current
[implementation checkpoint](UNIFIED_SYNC_IMPLEMENTATION_CHECKPOINT.md).

## Runtime and remaining visible work

**Current UI state, September12:** Root explicitly returned the completed
Ahoi-c8 window to Desktop and MBC (01a095a0). The visible Sync switch was set
OFF and read back0; ordinary application quit succeeded, with PID39235 absent
and no remaining own windows. Root holds no UI slot. The unused MBC START01a09524
was revoked; MBC's later11:50 handback was consumed once. None of these events
starts BetterConvo or revives September8 slots.

**First native visible result:** ordinary c8 MacA launch and navigation through
the existing Settings UI succeeded. CUA timed out, but permitted native macOS
Accessibility actions and own-window captures work. Root scrolled the actual
Ahoi Sync checkbox into view and changed it from0 to1. CloudKit is recognized;
the UI then reports an account/zone recovery prerequisite. AX exposes the two
account-recovery buttons with size0x0 and their Sync disclosure at0x28, so no
invisible confirmation was performed. Common's selective live flag readback
confirmed accountTransitionPending=true and zoneRecoveryPending=false. The
native AccountChange handler resets state even on first/identical sign-in;
Common is implementing a bound-account correction, preserving real-change
revocation. Desktop fixed the zero preferred width in6a2cd6f, with the recovery
disclosure shown without confirming anything. Source-only sidebar polish29b5f8a
also addresses the user's gray-pill screenshot. The misleading OFF-state
"CloudKit unavailable in this build" copy is part of the same focused followup.
Screens are under artifacts/e2e/native-sync-c8d9161-20260912/. This is a blocked
changed journey, NOT a bootstrap/transport/roundtrip pass. The bba96b17 scope may
now contain setup state and must never again be assumed unused from its name.
Device24 stays Sync OFF while this exact native prerequisite is resolved. The
next c8 correction combines those bounded fixes, not the separate Structure WIP.

No September8 Simulator reservation or START message is valid today. Before
starting, the current owner checks actual surfaces/workload and gives a concrete
current handoff where needed; an unrelated idle process is not a blanket block.
Desktop retains the sole native installation/UI path; no Mobile My-Mac host with
the same bundle ID is started implicitly.

Mobile19 icon/app start is already evidenced in `184adef`; do not repeat that
solely for branding. Host-label/recognized-metadata observations remain open and
can be folded into the next relevant candidate journey. Build18's Search/Restart/
Reset proof stays bounded to its old artifact, not full Sync acceptance.

Still open: the native first-opt-in/recovery path, visible device acceptance and
shared-key bootstrap/real encrypted roundtrip, native extension restoration/settings,
September12 domain extensions and their representative cross-client acceptance.
No Production publication is authorized or claimed here.

Detailed earlier receipts and abandoned wait states are retained in
[the September8 history](SYNC_COORDINATION_HISTORY_20260908.md). They are evidence,
not current instructions. Consult other old chronology only for a concrete gap.
