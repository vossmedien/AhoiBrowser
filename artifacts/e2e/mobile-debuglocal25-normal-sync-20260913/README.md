# DebugLocal25 normal Settings and Sync opt-in journey

The exact receipt-bound product candidate from clean source
`6446b534b3269befaf36a07fb80bde6e0251e745`, DebugLocal 0.1 (25), ran on the
Ahoi-owned iPhone 17 Pro / iOS 26.5 Simulator
`C645C09E-B284-434B-BC72-508E481ADC02`. The candidate receipt SHA-256 is
`358d8146c240d8297fffeeec52853d0050f194f0dfe03fa02759cf9387dc8921`;
its app-tree SHA-256 is
`2bd065664f717ace4641ac16a61f1ea9e937d3845e662e816067771299cad31e`.
The UI runner visibly verified the embedded source/build mode and executable
hash before continuing.

## Visible result

`AhoiMobileUITests/testDebugLocalSyncOptInStaysLocalAndFailClosed` passed
**1/1**, zero failures/skips, in 45.996 seconds. It used normal
`launchExactCandidate(arguments: [])`: no fixture, SyncProjection, injected
preference, SQL mutation or synthetic key state.

The visible journey:

1. started the normal blank browser;
2. opened Browser Actions and Settings;
3. enabled CloudKit Sync through the real switch;
4. confirmed `Nur lokal`, `Sync-Schlüssel sind deaktiviert`, the missing local
   provisioning/key explanation and disabled `Jetzt synchronisieren` action;
5. terminated and relaunched the exact app, then confirmed that the explicit
   opt-in persisted with the same provider-free state;
6. switched Sync off through the UI and confirmed the explanatory warning
   disappeared while `Jetzt synchronisieren` remained disabled.

Screenshots were exported from the successful XCResult and visually inspected:

- [`01-normal-browser-before-sync.png`](01-normal-browser-before-sync.png)
- [`02-provider-free-sync-opt-in.png`](02-provider-free-sync-opt-in.png)
- [`03-sync-opt-in-after-normal-relaunch.png`](03-sync-opt-in-after-normal-relaunch.png)
- [`04-sync-opt-out-restored.png`](04-sync-opt-out-restored.png)

The final XCResult is `normal-sync-ui-visible.xcresult`, its log/exit are
`normal-sync-ui-visible.log` and `normal-sync-ui-visible.exit`, and the parsed
summary is [`result-summary.json`](result-summary.json). XCResult tree SHA-256:
`ae2ae30ce482d6dfd24ce96934ae5651ef7bac8f173c632d116e38b4953bc5d4`.

## Retained red evidence and correction

The first `build-for-testing` ended `EXIT 65` because the shared scheme compiled
two unrelated stale Core-test files despite `-skip-testing`; no UI test ran.
The retained corrected runner builds exclude only
`SharedTabCreationProvenanceTests.swift` and `SharedTabFrozenContractTests.swift`.
Those tests were not executed or counted green.

The first two visible attempts also remain red (`normal-sync-ui.xcresult` and
`normal-sync-ui-fixed.xcresult`). Their AX snapshots showed the Sync row at
`y=854.x`, mostly below the 874-pt screen. The old helper treated that row as
hittable and tapped outside the display. Commits `289028b` and `2454d42`
ultimately require the real Settings form to scroll until the complete switch
frame is on-screen. The affected visible journey was then repeated and passed.

## Boundaries and cleanup

This is provider-free DebugLocal evidence, not CloudKit, iCloud-account,
cross-device or Structure-sync evidence. Public `simctl` inventory does not
expose a safe account-status result, and no Simulator-compatible entitled
CloudKitDevelopment candidate was built or run. The signed Build26 artifact is
`iphoneos`-only and was not used. Simulator iCloud-account readiness therefore
remains **NOT PROVEN**, rather than inferred from these screenshots.

The Ahoi simulator returned to `Shutdown` with Sync visibly off. BetterConvo
Simulator `751D9248-82F9-45D7-8C06-F910FFF3ACFC` remained booted and untouched.
No physical iPhone, My Mac, global input, CUA, Keychain injection, CloudKit,
Portal or provisioning action occurred.
