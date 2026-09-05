# Mobile shared-page identity foundation — 2026-09-05

This is acceptance of the **local Mobile identity/activation/save foundation**,
not acceptance of the complete shared normal-tab requirement in ADR 0007.
No Desktop source, shared wire schema, CloudKit server or TestFlight build was
changed by this package.

## Candidate and order

- Source: `30d73fab57fa667a5aca8a75df471d2996c100cc`.
- Version `0.1 (12)`, DebugLocal, iOS Simulator, ad-hoc signature verified.
- Clean detached build snapshot:
  `/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/repo`.
- Build, then visible XCTest UI journey, then targeted Core and repository
  checks. No programmatic behavioral tests ran before the visible journey.
- Simulator: iPhone 17 Pro, iOS 26.5 / 23F77,
  `CAE7F82B-52D2-4607-992C-EDF40C323DE3`. This existing Mobile fixture simulator
  was booted for the run and returned to its prior shutdown state afterward.
- The existing receipt-verified local HTTPS fixture and simulator-only test CA
  were reused; no new trust, TLS exception or user-keychain change was made.
- The initial `b6dfc73` build failed Swift 6 isolation checking for the nested
  commit callback. Explicit MainActor function types corrected it in `30d73fa`;
  the failed build result is retained, not relabelled.

The exact candidate receipt is [candidate-receipt.json](candidate-receipt.json).
It binds clean Git source, intrinsic Info.plist identity, toolchain, project,
signature and bundle/executable hashes. The UI runner verified those values
against the launched app's visible candidate label.

## Visible result

`MobileBrowserClosureRealE2EUITests/testSavePageToWorkspaceThenOpenFromTreeAndLibrarySearch`
passed **1/1**, zero failures/skips, on the exact candidate. The run created a
synthetic workspace through the Library, navigated a real local HTTPS page,
saved through the browser menu, navigated away, reopened through the saved-page
tree, navigated away again and reopened through Library search. The visible URL
and rendered document matched the saved destination on both returns. Tab-row
identity sets before/after both activations were equal: the original runtime
was reused, not duplicated. Cleanup removed only the newly created test
workspace and terminated the test app.

The final screenshot was directly inspected:
[saved-page-reopened.png](saved-page-reopened.png). This is a visible XCTest UI
journey, not a separate manual Computer-Use pass. Native Computer Use could not
initialize (`Sky Computer Use native pipe startup failed`); simulator and
XCTest screenshots supplied the actual rendered inspection.

## Subsequent focused checks

**11/11 Core tests passed**, zero failures/skips:

- `MobileTabReorderTests`: unique normal-only binding, UUID collision rejection,
  legacy decoding, preserving duplicate local records without duplicate shared
  authority, lazy references without WebKit/focus, distinct same-URL identities,
  and private reorder behavior.
- `MobileSharedPageSaveTests`: selection changes and reentrant save, binding
  before exposing the row, close/undo during save, private/failed save rejection,
  atomic repeat save/move without replacing identity, and tombstone preservation.
- `CompanionOperationFailureTests`: local store failure and outbound queue
  failure without duplicate or falsely failed local mutations.

`python3 -m unittest -v tests.repository.test_mobile_sync_event_contract`
then passed **2/2**, preserving provider-event ownership and the no-polling
contract. No broad unchanged suite was rerun.

## Still open for ADR 0007

The new local `treeNodeID` is not yet carried by DeviceTab/RemoteTab wire
records. Shared temporary nodes, their create/save/unsave/close semantics,
automatic arrival in an already-open peer, creator icons, legacy migration,
stable unassigned/empty-tab handling and the coordinated v3 capability/field
contract remain unimplemented/unaccepted. TabSwitcher persistence toggles still
need the authoritative shared mutation path. The Bookmark owner currently owns
the common C++ model/serialization/schema seams; Mobile has requested the
combined field/version handoff rather than editing those files concurrently.

The separate shared Chromium bookmark collection (ADR 0006), native C++ client
behavior, real cross-device Keychain bootstrap and Production Mac–iPhone–iPad
roundtrip are not proven by these local tests. Build 12 is not uploaded or
installed through TestFlight. Existing Build 10 distribution remains unchanged.

## Retained evidence

Raw logs, both build results, UI/Core results and UI attachments are retained at
`artifacts/e2e/mobile-shared-tabs-30d73fa/` in the canonical repository's ignored
artifact area. Hashes below use the existing domain-prefixed
`tools/mobile_evidence_artifacts.py:sha256_path` algorithm.

| Artifact | SHA-256 |
| --- | --- |
| `candidate-receipt.json` | `23e738c3fb946d3e6e85892320c5822784a57eb9d3a9fa80e010a0666dce714d` |
| `build-mainactor.xcresult` | `5c45136c9abf1bd4184b4bf429ad2344c4fba022433a1f099e7fd700a7ed42cf` |
| `saved-identity-ui.xcresult` | `c33221d8bce14bb1f22bd8a63d0bf871bba1186040a1e70a5d2134bcc47af3d2` |
| `saved-identity-core.xcresult` | `7318ae7411079fea4dcc4295ced4af546eafd2dff6b07d5ced36a664e16dc944` |

The incremental build outputs and clean snapshot remain under
`/private/tmp/ahoi-mobile-shared-tabs.V7PCPC/` while the coordinated shared-tab
package is pending. No Mobile build/test is active. Other simulators, native
Desktop candidates and pending signed CloudKit host outputs were not modified.
