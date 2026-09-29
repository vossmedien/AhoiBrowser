# Mobile private lock: successful unlock with retained page — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or skips**
on exact Xcode 27.0 DebugLocal build54
(`testLoadedPrivatePageUnlocksWithRetainedContentAndRelocks`).

The journey enabled the real device-authentication preference in Settings,
relaunched with one loaded private fixture tab ("Scale tab"), sent the app to
Home and returned. The native shield hid page and private address. "Entsperren"
started `LAContext.evaluatePolicy(.deviceOwnerAuthentication)`; SpringBoard
presented its compact `authentication_ui` element (134×291 pt, see
`system-auth-excerpt.txt`), i.e. the Face ID sheet, not the full-screen
passcode used in the earlier cancel journeys. The Simulator then received its
own biometric match event (`com.apple.BiometricKit_Sim.pearl.match`, the
equivalent of Simulator ▸ Features ▸ Face ID ▸ Matching Face). The same
private page and private address returned without reload to a new tab; a
further Home/return showed the shield again; terminating and relaunching the
process left no private tab to restore. All three screenshots were visually
reviewed.

Harness setup, not product behavior: the host enabled Simulator Face ID
enrollment (`notifyutil -s com.apple.BiometricKit.enrollmentChanged 1`) before
the run and delivered the match from the host via `simctl spawn notifyutil -p`.
Enrollment was reset to 0 afterwards so earlier passcode-cancel journeys keep
their setup; A168 was returned to Shutdown.

Diagnostic history: build53 (source `7105dbf`) failed RED (EXIT65) with the
shield still up and "Entsperren" disabled: the match event posted from inside
the XCTest runner via the Darwin notify center did not reach the Simulator's
biometric service. Source `ebc6748` adds the host-side delivery, a longer wait
and a SpringBoard hierarchy attachment; no product code changed. Build53
results remain local under `/private/tmp/ahoi-mobile-unlock.fcbD9I/`.

Build54 is clean detached source `ebc6748ef5a86c364684289597c0b7f7bccac3aa`,
DebugLocal 0.1 (54), simulator arm64, ad-hoc signed; deep signature verification
passed; built and post-test installed bundles were byte-for-byte equal and both
report source `ebc6748` and build54. Hashes in
[candidate.json](../../build/mobile-private-unlock-ebc6748-20260924/candidate.json).

Limits: Simulator biometric event, not a physical Face ID/Touch ID or a real
passcode entry; "without reload" is inferred from unchanged visible content,
not instrumented navigation counts. iPad multi-scene, VoiceOver and the
authentication-error path remain open. Not a Sync result; C645/CloudKit untouched.
