# Visible private-session return — 24 September 2026

Outcome: **one visible iOS 27 Simulator journey passed, zero failures or skips**
with Xcode 27.0 (27A266a). On the owned A168 device, the normal DebugLocal
app enabled private-session device authentication in Settings, opened a
private tab, went Home, returned, and presented the native full-screen
“Privater Tab geschützt” shield. The private address control was absent from
accessibility while shielded. The two exported screenshots show the private
tab before backgrounding and the shield on return.

Candidate: clean detached source `2feae1ec63a0452a072fc567d76bef3cc62fc405`,
`DebugLocal` 0.1 (41), `iphonesimulator` arm64, ad-hoc signed. The product-only
build succeeded, then the Xcode UI-test action rebuilt the executable; **the
tested candidate is the latter binary**, SHA-256
`c374d8c8f90ea1f8061d7f900891271a8ff87b40f8a041f76ad530d3215047a2`.
The installed bundle and archived test-built bundle compared byte-for-byte
identical with `diff -qr`; both carry the exact source commit and build 41 in
Info.plist. Deep signature verification passed. The app-tree digest and other
hashes are in [candidate.json](../../build/mobile-private-lock-2feae1e-20260924/candidate.json).

The one-test `xcresulttool` summary reports one pass, no skip or failure.
`xcodebuild.log`, `private-lock-visible.xcresult`, the product-only build
output and archived test-built app remain local evidence. The named screenshot
attachments are in `screenshots/` (see its manifest).

This proves only background-return shielding of a newly opened private tab on
this simulator. It does **not** prove unlock/cancel behavior, retention of an
actual private page after successful authentication, hardware Face ID/Touch ID,
physical iPhone behavior, or any CloudKit roundtrip. The simulator test opted
in via `AHOI_PRIVATE_LOCK_E2E=1`; no production setting or device auth secret
was injected. C645 and its CloudKit Development build 40 were not touched.

The smallest next private-lock E2E is to navigate to a local private page,
background/return, attempt cancel and successful unlock, and confirm the page
stays shielded on cancel and returns only after authentication on an exact
candidate. Physical-device authentication remains a separate gate.
