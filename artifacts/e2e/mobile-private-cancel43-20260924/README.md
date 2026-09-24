# Visible private-session authentication cancellation — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or
skips** using Xcode 27.0 (27A266a). On the owned A168 iPhone 17 Pro simulator,
the normal DebugLocal app enabled its device-authentication setting, opened a
private tab, went Home and returned to the native shield. Tapping “Entsperren”
displayed SpringBoard's real full-screen iPhone-code prompt. The test tapped
the system “Abbrechen” button, then verified the shield still existed and the
private address control remained absent from accessibility. The two exported
screenshots show the prompt and the still-protected tab after cancellation.

Candidate: clean detached source `9a4923009ab46f4278904fea099c681e6731bf15`,
`DebugLocal` 0.1 (43), `iphonesimulator` arm64, ad-hoc signed. The explicit
`build-for-testing` and subsequent `test-without-building` ran against the
same source-stamped app. Xcode reinstalled that app for the test; the final
installed bundle compared byte-for-byte identical with the built bundle using
`diff -qr`. Both carry source `9a49230` and build 43 in Info.plist. Deep
signature verification passed. Binary SHA-256:
`a75f850b03b536a0c3818e4816f721598625fea738bdd09146f304184ff2b497`.
The app-tree digest and other hashes are in
[candidate.json](../../build/mobile-private-cancel-9a49230-20260924/candidate.json).

The one-test `xcresulttool` summary reports one pass, no failure or skip.
The built app, build log, result bundle and test log are retained as local
evidence; the two safe UI screenshots and their attachment manifest are in
`screenshots/`. The first test version, source `c489241`, failed because it
queried SpringBoard's authentication screen as an alert. The actual UI was a
full-screen `authentication_ui`; source `9a49230` corrected only that test
query and the exact rebuilt Build43 journey passed. This was a test-harness
correction, not evidence of a product fix.

This proves cancellation keeps a newly opened private tab protected on this
simulator. It does **not** prove successful authentication, restoration of a
loaded private page, hardware Face ID/Touch ID, physical-iPhone behavior,
CloudKit or Mac–iOS Sync. The UI test opted in via
`AHOI_PRIVATE_LOCK_E2E=1`; no authentication secret was injected. C645 and its
CloudKit Development build40 were untouched. The runner environment was
removed and A168 returned to Shutdown. Build43 and the enabled private-lock
preference remain in that dedicated simulator's app data.

The smallest next E2E is a real private-page load, Home/return, successful
system authentication and a check that the original page—not merely an empty
private tab—returns. Physical-device authentication remains a separate gate.
