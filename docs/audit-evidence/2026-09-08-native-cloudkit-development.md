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
Development branch still needs actual prepared-candidate execution/readback.

## Next actual gate

Finish the one combined toolbar/icon/settings candidate, keep original receipts,
copy its exact bundle, run `prepare-macos-cloudkit`, sign that copy with the
existing Apple Development identity and exact generated entitlements, then
`verify-macos-cloudkit`. No second Chromium compile is required for preparation.
Install/run only under Desktop's normal candidate/profile safeguards.

Mac's payload-key loader only reads an existing synchronizable data-protection
32-byte key. Bootstrap remains a real gate, coordinated with the existing
Mobile/Common key lifecycle; no independent random key, plaintext export or
ad-hoc key injection is substituted. No CloudKit zone/key/record mutation,
cross-client roundtrip or Production readiness is claimed by profile creation.

Browser Portal navigation reached Apple's login, then Chrome reported an open
extension surface blocking automation. No credentials were entered there.
The legitimate existing Xcode account route completed provisioning without
bypassing that browser surface or requiring a new general approval.
