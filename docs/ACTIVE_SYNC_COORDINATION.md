# Active sync coordination

Updated: 2026-09-13. Root coordinates the same canonical branch,
`codex/desktop-core-feature-wave-20260830`. The user explicitly confirmed both
former Terminal agents stopped and authorized restarting the two workstreams.
Their old thread IDs and September8 leases are not current runtime grants.

## Current parallel-work rule — explicit user decision, September13

The user explicitly removed the blanket MBC/Ahoi window reservations and repeated
START/handback coordination: parallel work is intended, with Ahoi taking priority
in this task. No worker waits for MBC's whole build or UI journey. Separate
builds, devices and directly addressed windows may run concurrently. Only actual
sustained machine pressure or a concrete collision over global input warrants
separating the affected short action. No foreign process is stopped and no
general START/ACK cycle is required. This supersedes the dated UI leases below.

`/root/desktop_recovery_20260913` proceeds autonomously through the exact guarded
build/sign/install and short native E2E with fresh candidate, scope and own-window
checks. Verify the corrected OFF label/sidebar, then visible Sync ON and, if
needed, the authorized local-upload recovery in MacA only. Prefer direct AX
actions and own-window captures, not unverified global keystrokes. No hidden AX
action, flag/key/store reset or real-Default operation is authorized. Existing
file/build ownership, consent and data-safety boundaries remain intact.

## Actual recovery and current workers — September13

**Current installed result is26b01b1, not the failed predecessors below.**
Build65225, scoped sign/verify65919 and install69712 completed EXIT0. The real
retained-MacA journey performed a visible manual retry before the old deadline,
then one justified follow-up with server metadata. The same process stayed
stable and displayed "Synchronisiert und bereit" before ordinary quit.
Root independently read the actual installed26 source and post-quit database:
outbox0, acknowledged16, native_observations2; retry0/0/0/empty and both recovery
flags false. Bookmark consent remains revoked. This proves the native MacA
upload/ACK boundary, NOT a peer/physical Mac–iPhone roundtrip. Exact receipts,
counts and retained failures are in the current
[Desktop checkpoint](ACTIVE_DESKTOP_CHECKPOINT.md) and
[26 runtime report](../artifacts/e2e/native-sync-26b01b1-20260913/README.md).
Canonical product commits614d297/2948361/628d163/f7d276c/6fee878 preserve these
corrections; old55/178 builds, failures and retry deadlines are historical.

Root has asked the user for the one ordinary physical action needed on the
confirmed installed Device24: open Ahoi Settings on Servusla, enable CloudKit
Sync and report the status without account/key details. Until an actual response
or readback, Phone opt-in is unproved. No manual key copy, zone/store reset or
artificially seeded phone state is allowed.

**Next independent product gap:** read-only inspection of the exact26/Device24
sources found that Mobile already schedules domain import/projection after
unsolicited CK fetches, while Native only persists its inbox and waits for a
later startup/manual/local/five-minute sync. That is not the required live
arrival in an already open native window. Desktop is explicitly implementing
the minimal durable-inbox -> provider signal -> existing domain import/native
projection wake, without a new engine or a faster polling timer. Receive-only
work must not wait for an outgoing backoff or clear it as a fake upload success.
The active bounded scope includes the existing provider/interface/factory,
pump/backend/service, and the necessary SyncStore import/precommit lease plus
preserve-outgoing-retry option. Coalesce events, retain original consent and
generation through import/ACK, and never focus or eager-load peer pages.
No other Common/Structure WIP is transferred. This next source work must not
overwrite the preserved26 candidate or be called a live-peer pass before E2E.

The previous two workers were no longer in the live collaboration inventory.
The old `/private/tmp/ahoi-native-sync-build.eqejEO/repo` is missing; the Git
worktree registration is stale, NOT a running or resumable process. Its exact
e4de9e14ca3073876e9aa17730876d15cfa41683 commit and original logs survive.
No cause of the missing directory was established or attributed to the storage
owner. Nothing was pruned or reset.

Desktop recovery secured the commit under
`refs/ahoi-preserved/native-sync-e4de9e1-20260913` and restored a clean detached
snapshot at `/private/tmp/ahoi-native-sync-recovery.DxdGLK/repo`. Its new guarded
overlay/app-only run is82567, with logs/exit/receipt under
`artifacts/build/native-sync-e4de9e1-recovery-20260913/`. Initial capacity was
68 percent CPU idle, no compiler,94 GiB free and0 swap; three build jobs were
selected. This is a NEW recovery invocation, not a claimed terminal result or
resumption of September12 PIDs. Desktop supplies its true terminal state.
The storage owner received this exact new protected path in01a0982b.

**Actual subsequent result:** app-only82567 completed EXIT0. Root read its
clean-source receipt and verified SHA
ee559365e251b10f7f6a49d60e18f61466a14048f44634e3ae84b2d551d51e37.
Separate Development sign/verify26850 and guarded install42266 completed EXIT0.
Root independently read installed e4, Development and the bba zone, and verified
installed executable SHA
4ca4ef074b1e28bdad8f01a0695ffba596136ed7d0fc71484a1c4321d6217dd9.
Install receipt `artifacts/install/ahoi-dev-e4de9e1-scoped-20260913.json` has SHA
eb54ab0e73174286e0fd3fb06e60bf1e3e661f370a68a59a84b817707047d245.
The bounded e4 journey fixed OFF/ON hierarchy and made the recovery action
visible. After the deliberate local-upload confirmation, account recovery read
back false, but the browser then crashed. Desktop symbolized the fatal path to
`SyncStore::AcknowledgeOutbox`: unsupported UPSERT created an invalid Chromium
statement. Root confirmed the pinned `SQLITE_OMIT_UPSERT` setting. The second
same unsupported SQL form was in the native observation receipt journal.
This is still a failed runtime journey, not transport/roundtrip acceptance.

Desktop received exact ownership of those two Common SQL files. Isolated
ccd24827be87ab2cdcc0e2579137eb7d83700b4b preserves the same schema, transaction,
clock condition and receipt semantics with supported SQL. Its app-only41122,
scoped sign/verify14386 and guarded install57636 completed EXIT0. Root read the
clean build receipt3ad7f306987b090ee23e07d108f88e17a2a6875936303e6cb0b5f47b4e028160
and independently verified the installed ccd source. The two SQL fixes are now
canonical614d297; old e4 and crash evidence remain preserved. No key/store reset
or SQLite build-flag change occurred.

**ccd runtime remains PARTIAL:** the normal retained-MacA start/restore/visible
Sync Now journey was stable and normally quit. Real post-quit state was13
outbox rows,0 ACKs,0 native receipts, retry9/provider_error, with recovery flags
false. Existing public CloudKit logs identify CKErrorDomain2/PartialFailure,
not its redacted item error. The later focused check against the exact Chromium
SQLite library verified the two corrected SQL statements and clock ordering;
it does not prove a real ACK. Details are in
[the ccd report](../artifacts/e2e/native-sync-ccd2482-20260913/README.md).

Root reviewed and explicitly released only the provider HandleSent /
CompleteUpload files for the next correction: a logically resolved server
conflict can collect verified ACKs that the aggregate PartialFailure then
discards. Do not blindly ignore partial failures; current mutation IDs,
remaining item/zone failures, durable remote staging and original authorization
must remain authoritative. A small local code/count/stage diagnostic may
distinguish this path without logging identifiers, payloads, keys or raw errors.
No third runtime loop without that concrete change; other Common WIP remains
unassigned. Device24 stays OFF, freshly confirmed installed on Servusla.
Earlier c8 below is retained baseline history.

Root verified that the e4 correction changes no shared format manifest, model,
goldens or Swift/Mobile source relative to c8. The preserved signed Device24
bundle is present and its actual plist matches all seven shared scope values.
It remains the schema6 partner; this check is not a new physical-device or
CloudKit pass. The manifest's original prepared-status text is historical,
not permission to treat the already exercised bba scope as empty.

The separate Mobile worker completes only the retained Home/Reader/Markdown
product WIP in `apps/AhoiMobile`, not another runner or test matrix. Its source
belongs to the later Structure/Privacy wave, not the current Device24/e4 pair.
That bounded source package is now committed/pushed as8e81cfc: explicit Home
update/return, article extraction/Reader sheet and safe Markdown-link copying
through normal browser actions, with private/local clipboard handling and
DE/EN. Root read the actual product wiring. Syntax/configuration checks passed;
it is NOT_BUILT/NOT_E2E and no longer an active worker task.

**Physical UI boundary, September13:** Servusla is reachable and unlocked since
boot but currently requires its passcode. The ordinary Mirroring app binding
did not establish a connection; the normal Finder check found no selectable
Mirroring app and was returned to its original folder. No phone, account or
security setting was changed. [Apple's current support page](https://support.apple.com/en-ca/120421)
also states that iPhone Mirroring is unavailable in the EU; this is not proof
of a specific local account error. No region bypass or replacement UI harness
was attempted. The actual phone opt-in/observation still needs its normal
device UI after the native prerequisite succeeds. CUA's ordinary native Finder
binding/actions now work; old September12 CUA timeouts are not current evidence.

## New user UI/UX direction — selection pending

The user explicitly rejected the entire Sync form, its sidebar position and
layout, then requested substantially better general UI/UX with ImageGen layouts.
Root produced three independent, reference-grounded concepts via the built-in
ImageGen tool. All three were inspected and displayed exactly once in the order
recorded in [the design handoff](design/2026-09-13-browser-sync/README.md), with
byte-identical project copies and the full prompt set. They are NOT implemented
or runtime evidence; their connected-device/status examples are mock data.

The common design direction is ordinary central Sync settings, one clear global
control, readable data/device groups and remote-control technical fields kept
separate from normal setup. Sidebar/toolbar integration stays compact; no
large wizard, new transport or arbitrary data-category expansion. User selection
of the displayed1/2/3 establishes the visual target before redesign code begins.
The proven native SQL crash fix continues independently and must not wait for
that visual choice. Mobile8e81cfc remains a separate unbuilt source packet.

## Bounded compiled-artifact release — September13

The storage owner019e5926 has explicit release for exactly these six superseded
compiled bundles, after its own fresh use/path checks. Actual bundle plists
match the named source revisions and their build/signing receipts remain present:

- `artifacts/build/desktop-arc-preserve-e241191-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-arc-preserve-e241191-20260908/cloudkit/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/cloudkit/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-left-715afc2-20260908/AhoiBrowser.app`
- `artifacts/build/desktop-toolbar-left-715afc2-20260908/cloudkit/AhoiBrowser.app`

No surrounding directory, source, log, receipt, evidence or external provisioning/
key material is released. c8 artifacts, all `.work` checkout/output, the existing
native-sync temporary snapshot and its pending correction, `/Applications` and
ALL rollback bundles remain protected. The historical instruction to retain the
then-newest e241 CloudKit copy is superseded only for the exact artifact bundle
above. Root performed no deletion, build, test or process stop; release is not
evidence that cleanup occurred or that a measured amount of space was reclaimed.

## Current owners

| Assigned owner | Exclusive product scope |
| --- | --- |
| `/root/desktop_recovery_20260913` | Current exact e4 recovery: isolated source, shared Chromium checkout/out, guarded app build/sign/install and the bounded native UI handoff above |
| `/root/mobile_actions_recovery_20260913` | Completed source8e81cfc; idle, no build/device/signing or Common/Native ownership expansion |
| `/root` | Coordination and acceptance, this checkpoint; no competing product implementation |

Workers use the current collaboration tools. The old desktop_resume and
sync_mobile_resume assignments are preserved history; unassigned Common and
Native Structure WIP must not be taken over implicitly. Existing uncommitted
September12 product-contract edits remain untouched. Global/project AGENTS apply.

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

**September12 UI history, superseded by the current handoff above:** Root explicitly returned the completed
Ahoi-c8 window to Desktop and MBC (01a095a0). The visible Sync switch was set
OFF and read back0; ordinary application quit succeeded, with PID39235 absent
and no remaining own windows. Root holds no UI slot. The unused MBC START01a09524
was revoked; MBC's later11:50 handback was consumed once. None of these events
starts BetterConvo or revives September8 slots.
MBC subsequently started a genuinely new8998792A session (launchd_sim77724,
freshly identified on September12). Its natural handback is requested in
01a095c1 before the next native Sync UI journey. This is not an old-slot hold.
Its brief boot-time capacity spike subsided and Desktop resumed its own paused
preflight after two sufficient samples; no continuing blanket CPU block exists.

**First native visible result:** ordinary c8 MacA launch and navigation through
the existing Settings UI succeeded. CUA timed out, but permitted native macOS
Accessibility actions and own-window captures work. Root scrolled the actual
Ahoi Sync checkbox into view and changed it from0 to1. CloudKit is recognized;
the UI then reports an account/zone recovery prerequisite. AX exposes the two
account-recovery buttons with size0x0 and their Sync disclosure at0x28, so no
invisible confirmation was performed. Common's selective live flag readback
confirmed accountTransitionPending=true and zoneRecoveryPending=false. The
native AccountChange handler reset state even on first/identical sign-in.
Common793d58a now binds that event to the account identity already verified by
bootstrap, preserving real-change revocation and existing recovery flags.
Desktop fixed the zero preferred width in6a2cd6f, with the recovery
disclosure shown without confirming anything. Source-only sidebar polish29b5f8a
also addresses the user's gray-pill screenshot. The misleading OFF-state
"CloudKit unavailable in this build" copy is corrected in30e2a1a.
Screens are under artifacts/e2e/native-sync-c8d9161-20260912/. This is a blocked
changed journey, NOT a bootstrap/transport/roundtrip pass. The bba96b17 scope may
now contain setup state and must never again be assumed unused from its name.
Device24 stays Sync OFF while this exact native prerequisite is resolved. The
next c8 correction combines those bounded fixes, not the separate Structure WIP.
Its initial a95 compilation found one protected LabelButton API in the sidebar
polish; Desktop10ee5e8 uses the public API. The exact corrected candidate and
actual guarded build state are in the Desktop checkpoint, not a duplicated
handle here. For the isolated MacA test data only, Root has explicitly chosen
the visible "Lokale Daten weiter hochladen" recovery option after candidate/
scope/visibility verification. No hidden flag reset or real-Default upload is
authorized by that choice.

The physical Device24 UI access preparation found no validated attach-only
runner for those installed bytes. Existing fixture/seed/launch-based UI tests
must not be repurposed as this live-device journey. The current normal device
entry is More -> Settings -> CloudKit Sync, with an actual device screenshot/
observation still required; do not infer it from devicectl or local JSON. No
Phone opt-in was performed. Independent Mobile Home/Reader/Markdown work proceeds
in its source ownership while the native correction runs, without another
harness or an uncoordinated hardware/CloudKit action.

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
