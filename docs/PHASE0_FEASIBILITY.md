# Phase 0 feasibility record

Snapshot date: 2026-08-30. This record separates what is technically available
in Chromium from what AhoiBrowser is authorized and able to distribute.

## Extension platform and Chrome Web Store — green/yellow

Ordinary Chromium MV3 installation and update paths can support Chrome Web Store
extensions in a Chromium-based browser. The store, its branding, APIs, and
availability remain Google-controlled. Chrome-private services are not inherited:
Google blocks Chrome Sync/private APIs in third-party Chromium products, and
`chrome.identity.getAuthToken()` normally fails outside Google Chrome.

Primary references:

- <https://support.google.com/chrome_webstore/answer/1698338>
- <https://developer.chrome.com/docs/extensions/how-to/distribute>
- <https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html>
- <https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/api_keys.md>

Phase-0 probe `P0-EXT-CWS`: use a signed app in `/Applications`; visibly install,
restart, update, disable, and remove a public MV3 extension; then test 1Password,
Bitwarden, native messaging, DevTools integration, and the expected
`getAuthToken` limitation. Repeat on every Chromium roll.

1Password documents a user-driven Additional Browsers flow for signed apps in
`/Applications`, which makes a real Phase-0 test plausible without pretending
Ahoi is already vendor-allowlisted: <https://support.1password.com/additional-browsers/>.

## Selective uBlock Origin Classic — yellow

General MV2 is not restored. Chrome removed the legacy enterprise escape path,
and remaining legacy-distribution items are scheduled for removal on
2026-08-31. Ahoi maintains an extension-ID-bound exception and the smallest
required runtime seams. The product bootstrap uses the **Official GitHub
release** `1.74.0`, full release commit
`6dd2d95e50d134a477a4e183343c0b26e9147123`, and key-derived ID
`fkgkibajhfbepljeaefdnfnegdcjomkh`.

The signed browser statically pins the complete CRX SHA-256
`b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e`
and CRX public-key SHA-256
`5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf`.
A manual initial check creates the exact entry without a catalog network
request. Download begins only at the pinned release URL, accepts at most one
credentialless GitHub release-asset redirect, and then requires hash, CRX3 key,
derived ID, version, normal Chromium permission prompt, and atomic local
authorization checks. The separately signed catalog remains unprovisioned and
is reserved for later updates.

Primary references:

- <https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline>
- <https://github.com/gorhill/uBlock/releases/tag/1.74.0>
- <https://chromium.googlesource.com/chromium/src.git/+/df12ca035ced7f8026c717eea14a4b406e9df574%5E%21/>

Phase-0 probe `P0-UBO-MV2`: fetch the exact pinned Official GitHub release CRX
without rebuilding, repacking, or re-signing it; verify its complete hash,
public-key hash, derived ID, release commit, and bounded redirect; reject a
second random or unpacked MV2 package; and exercise network/cosmetic filtering,
popup, logger, picker, custom filters, list updates, restart, later signed
extension update, tamper, downgrade, and kill switch. Public redistribution
remains disabled pending license/name/logo and redistribution review.

## Widevine — red until contract

Chromium contains EME/CDM integration but not a redistributable Widevine CDM.
A company-domain Google account, Widevine Master License Agreement, partner
access, permitted ARM64 package, and compliant update path are external release
gates. A Chrome-installed CDM must never be copied.

- <https://developers.google.com/widevine/drm/overview>
- <https://developers.google.com/widevine/access>
- <https://chromium.googlesource.com/chromium/src/third_party/+/refs/heads/main/widevine/cdm/BUILD.gn>

`P0-WIDEVINE` starts with ClearKey. After authorization it verifies Widevine
test content, host verification/update, then ASSISTED_E2E Netflix and a second
service. No contract is `BLOCKED_ENTITLEMENT`; no account is
`BLOCKED_CREDENTIAL`, never PASS.

## H.264/AAC — red until distribution review

`proprietary_codecs=true` and Chrome FFmpeg branding can technically expose MP4,
H.264, and AAC, while VideoToolbox can hardware-decode on macOS. Those flags do
not grant patent rights. Via LA maintains distinct AVC/H.264 and AAC programs;
the effect of relying on Apple frameworks needs written specialist confirmation.

- <https://via-la.com/licensing-programs/avc-h-264/>
- <https://www.via-la.com/licensing-programs/aac/>
- <https://chromium.googlesource.com/chromium/src/media/+/refs/heads/main/gpu/mac/video_toolbox_video_decoder.cc>

`P0-CODEC` compares an open-codec control with a private evaluation build and
checks canPlayType, MP4/MSE/WebRTC/PiP and VideoToolbox evidence. It cannot be
published before the legal gate passes.

## Apple distribution and updater — green

Developer ID, Hardened Runtime, nested Chromium-helper signing, notarytool,
stapling, Gatekeeper verification, and DMG delivery are the correct external-
store path. Chromium's macOS signing scripts are the base; `codesign --deep` is
not a packaging strategy. Release helpers receive only the minimum JIT and
runtime entitlements required by their process role.

- <https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution>
- <https://developer.apple.com/support/developer-id/>
- <https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/installer/mac/signing/>

Sparkle is the implemented updater, pinned at security-fixed `2.9.6`; profiling
is off, keys remain outside source/web hosting, and full/delta/fallback/tamper/
interruption/rollback tests are mandatory. The prior 2.9.5 candidate is not
shippable because the two high-severity advisories below affect 2.9.5 and older.

- <https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6>
- <https://github.com/sparkle-project/Sparkle/security/advisories/GHSA-3x7w-j75x-ppq5>
- <https://github.com/sparkle-project/Sparkle/security/advisories/GHSA-4v99-qgq9-6pxp>
- <https://sparkle-project.org/documentation/security-and-reliability/>

## CloudKit and companion — green/yellow

Developer-ID macOS apps can use CloudKit with a matching provisioning profile.
Mac and iOS require registered bundle IDs, one Team and CloudKit container.
Private custom zones and CKSyncEngine fit the local-first design. CloudKit push
is best effort; the five-minute remote-command TTL is an expiry/replay rule, not
a delivery guarantee. `CKRecord.encryptedValues` cannot be queried or sorted, so
UUID/HLC/order metadata stays separate from encrypted payload fields.

- <https://developer.apple.com/support/developer-id/>
- <https://developer.apple.com/documentation/cloudkit/cksyncengine>
- <https://developer.apple.com/documentation/cloudkit/ckrecord/encryptedvalues>

The native SwiftUI companion embeds no engine and therefore needs no alternative
browser-engine entitlement. `P0-APPLE-CLOUD` proves signing/notarization plus a
real Mac↔iPhone create/update/delete/encrypted-value roundtrip, offline conflict,
tombstone, signed command, nonce, replay, expiry, push loss, and device revocation.
