# Loaded private page: cancel, repeat background, process end — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or
skips** on the exact Xcode 27.0 DebugLocal build48. A selected private WebKit
tab visibly contained the deterministic local “Scale tab” document. Home and
return showed the native full-screen shield; “Entsperren” opened SpringBoard's
real device-authentication prompt, and “Abbrechen” kept the document and
private address control inaccessible. A second Home/return still exposed only
the shield. After terminating and normally relaunching the process, there was
no private tab or private document to restore. The page was a debug-only
synthetic WebKit fixture, not a real network website or proof of successful
unlock retaining its content.

The accepted test is `testLoadedPrivatePageStaysShieldedAfterCancelAndSecondBackground`
on clean detached source `40406054d2b82ca5ed4d9f3b969270e24ce0fd37`,
`DebugLocal` 0.1 (48), `iphonesimulator` arm64. `build-for-testing` succeeded;
`test-without-building` then ran only this test twice, each time 1 passed, 0
failed/skipped. The second run, `loaded-visible48b.xcresult`, supplied the
accepted visual evidence. The final installed bundle was byte-for-byte equal
to the built app after Xcode's test reinstall (`diff -qr` exit0); both carry
source `4040605` and build48 in Info.plist. Deep ad-hoc signature verification
passed. Binary SHA-256:
`6b2921f7e7b8dc1319cdccde35db499da546c7e970baece8bf612096026333c0`.
Other hashes are in [candidate.json](../../build/mobile-private-loaded-4040605-20260924/candidate.json).

The two exported XCTest screenshots show the loaded private fixture before
backgrounding and the shield after authentication cancellation. A separately
captured `simctl io screenshot` shows the shield still rendered about four
seconds after the test's second-return marker, *before* its planned process
termination. The runner log timestamps are 17:51:33.376 for
`AHOI_PRIVATE_LOCK_SECOND_RETURN_READY`, 17:51:37 for the external screenshot,
and 17:51:42 for the subsequent app relaunch. The screenshot contains no
private page content. Safe images and attachment manifest are in `screenshots/`.

Exploratory Builds44–47 exposed a screenshot-method artifact: after an XCTest
`app.screenshot()` at the second return, later XCTest and some independent
captures showed a white frame despite the shield still present in
accessibility; an independent Build46 capture did show the shield. Build48
removed that second-return XCTest capture and preserved the same product code.
The controlled Build48 external capture showed the shield at the delayed
checkpoint. Thus the white diagnostic frames are **not** accepted evidence of
a normal product-rendering failure, but the interaction between XCTest capture
and compositor remains a test-harness caveat. Raw diagnostic result bundles
and white frames stay local rather than being promoted as acceptance images.

The built app, build log, accepted result bundle, runner log and test bundle
are retained locally. `AHOI_PRIVATE_LOCK_E2E=1` and the optional
`AHOI_PRIVATE_LOCK_CAPTURE_HOLD=1` only controlled the test runner; no device
authentication secret was injected. C645 and its CloudKit Development build40
were not touched. Both opt-ins were removed and A168 returned to Shutdown;
Build48 and its enabled private-lock preference remain in that dedicated
simulator's app data.

Still open: successful device authentication with the same loaded private
page restored, failure/retry, iPad scenes, VoiceOver, real network page,
physical Face ID/Touch ID and Mac–iOS Sync. This is a bounded part of
WORKFLOW-08/DoD27, not full acceptance.
