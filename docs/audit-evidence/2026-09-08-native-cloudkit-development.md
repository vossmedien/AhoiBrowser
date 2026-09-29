# Native Mac CloudKit Development — 2026-09-08

## Actual progress, not a transport pass

The user's explicit authorization covers necessary Development resources in
the existing Apple account; no Production publication or destructive key/profile
action. Xcode26.6 already had the Christian Voss Admin team account. A normal
manual-profile download completed; it did not supply a matching Mac profile.

The existing Xcode account then successfully created a Mac Team Development
profile using a disposable single-target native macOS project with Ahoi's
existing bundle ID, container and exact keychain group requests:

- UUID: `8f149b92-89cc-4d34-a0db-1b305d4e545c`.
- Path: `/Users/vossmedien/Library/Developer/Xcode/UserData/Provisioning Profiles/8f149b92-89cc-4d34-a0db-1b305d4e545c.provisionprofile`.
- SHA256: `876521345f4eaee8de1455c39ef6bb0574d90151cb5d813a3a293bf33d8d2445`.
- PlatformOSX, Team248AJ5BN47, exact App ID
  `248AJ5BN47.app.ahoibrowser.AhoiBrowser`, one provisioned Mac.
- Dedicated container `iCloud.app.ahoibrowser.AhoiBrowser`, APNsdevelopment.
- Created2026-09-08T11:31:29Z; expires2027-09-08T11:31:29Z.
- Existing Apple Development identity was reused; no certificate/private-key
  export, deletion or replacement. No key bytes were read or printed.

Provision-only command: existing `xcodebuild`, target/scheme
`AhoiMacDevelopmentProvisioning`, Debug, `platform=macOS,arch=arm64`, jobs2,
`-allowProvisioningUpdates -allowProvisioningDeviceRegistration build`.
Session10142 EXIT0. Inputs/DerivedData are under
`/private/tmp/ahoi-mac-provisioning.ktjShE`; main merely returns0. The app was
never installed toApplications or launched. Xcode performed its ordinary
LaunchServices registration of that build output; it was subsequently removed
with `lsregister -u` against ONLY that exact provisioning-app path, EXIT0.
Small inputs are preserved in the canonical artifact directory below; the
profile itself remains in Xcode's normal cache. No other app registration,
profile, certificate or key was removed.
Canonical log: `artifacts/build/macos-cloudkit-development-20260908/provisioning-build.log`.

## Corrected tooling boundary

The first actual profile validation exposed a tooling failure, not an Apple
account failure: the old validator required profile allowlists to equal signed
app claims. Apple's native Mac profile permits both environment values, services
through`*` and keychain groups through the Team prefix; it omits the unrestricted
macOS task-allow field. [Apple TN3125](https://developer.apple.com/documentation/technotes/tn3125-inside-code-signing-provisioning-profiles)
explicitly distinguishes a profile allowlist from actual signature entitlements.

Tooling correctionf4aee9d understands only these bounded profile forms. AppID,
Team, container, APNs, certificate inventory, dates and device shape stay checked;
the signed app MUST still claim exactDevelopment, exactly the two configured
Ahoi groups and no wildcard/foreign rights. Actual new profile readback passed.
Three small profile/claim boundary checks passed after that real readback.

Component Development also retains the existing portable manifest/deep-signature
path, not a partial hardened-runtime signature that would reject its linker-
signed libraries. The new explicit Development verifier combines that existing
candidate check with exact signature/profile/certificate/runtime configuration.
Production's full hardened-runtime signer/verifier remains unchanged. This
Development branch has now passed actual prepared-candidate verification below.

## Exact prepared and signed native candidate

The combined toolbar/icon/settings app-only build62333 is TERMINAL EXIT0 at
source `3d59cf9de8846e47c91db540b90b3067b18c0841`. Its original build receipt,
bundle and binary were preserved. An APFS clone was prepared and signed ONLY at
`artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/cloudkit/AhoiBrowser.app`.
Preparation91993 EXIT0, signing25981 EXIT0, verification6196 EXIT0. No second
Chromium compile, installation, profile launch or CloudKit call was required.

- Original build receipt: `build-receipt.json`,
  SHA256 `f5b04cebd2290ad3d7c29baa3ae2a1955d7942d16bd427779965d1e73025f6f8`.
- Preparation receipt: `cloudkit/preparation.json`,
  SHA256 `e1213a65e2d711998751dd12f35ad7844a6a6e540df03954de6ed28f2c53f713`.
- Verification receipt: `cloudkit/verification.json`,
  SHA256 `6da4cea119d4e19c89d4f68292f6a6289ffb4f32b7ffac78c83b8f5511a33597`.
- Signed copy executable:
  `7eedd882b85761c8fd04eab6c88c20f7bc21d1ebc234da39112bbe6001824a20`.
- Signed copy bundle tree:
  `d987e11659044e594cbba97a9f36097b7e89eedd7952fd43e95feb827dc6b05f`.
- Actual public signing-leaf SHA256:
  `32588cf8f454c31e32e62743f10731c9f394bfe8c84098bfbcf980a40281550c`,
  present in the embedded profile certificate inventory.

All receipt paths above are relative to the canonical
`artifacts/build/desktop-toolbar-settings-3d59cf9-20260908/` directory (the
original build receipt is directly in that directory). Signed runtime has the
dedicated Ahoi container, exact payload/command groups, Development environment
and native development APNs. Existing portable component manifests, nested code,
top-level identity, entitlements, certificate and runtime values were checked.
`releaseEvidenceEligible=false`; no notarization/Production acceptance.

The first verifier54382 EXIT2 reached certificate readback but passed the optional
`codesign --extract-certificates` prefix as a separate argument. This codesign
treated it as another input path. The exact attached form
`--extract-certificates=<prefix>` exposed the public leaf correctly. One tooling
line was corrected; no warning/profile/certificate check was removed. The full
real CLI then passed. The exact fix was temporarily applied only to signing.py
in the3d59 tool snapshot and removed after the run; its Git HEAD/product source
and built app were not relabelled. Canonical tooling retains the correction.

## Next actual gate

The prepared copy is not installed or run yet. Complete the protected Arc
recovery in compatible4cb, then use the guarded installation and a short visible
toolbar/settings journey on the new exact candidate. Native bootstrap and a
matching-client fresh isolated Development roundtrip remain open.

Mac's payload-key loader only reads an existing synchronizable data-protection
32-byte key. Bootstrap remains a real gate, coordinated with the existing
Mobile/Common key lifecycle; no independent random key, plaintext export or
ad-hoc key injection is substituted. No CloudKit zone/key/record mutation,
cross-client roundtrip or Production readiness is claimed by profile creation.

Browser Portal navigation reached Apple's login, then Chrome reported an open
extension surface blocking automation. No credentials were entered there.
The legitimate existing Xcode account route completed provisioning without
bypassing that browser surface or requiring a new general approval.
