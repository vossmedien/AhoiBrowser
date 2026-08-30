# Releasing

Channels are `nightly`, `beta`, and `stable`. Every channel uses signed update
metadata and an independently verifiable application signature. Stable is never
promoted solely from CI; installed-app E2E evidence and the release checklist
are required.

`config/version.json` separates the numeric Apple marketing version and
monotonic build number from the nightly/beta/stable channel and the human-facing
development version. Before signing, the built app is stamped with all four as
well as the Ahoi source commit, Chromium version/commit, build profile, and the
exact matching GN-argument hash. Installed release evidence rejects a `dev`
profile even if it is otherwise signed correctly.

## Candidate pipeline

1. Verify clean Ahoi commit and pinned Chromium/depot_tools/toolchain manifests.
2. Build unmodified Chromium control and optimized non-component AhoiBrowser
   for ARM64 without prohibited flags.
3. Run unit/integration/security/license/secret scans.
4. Sign nested code and app with Hardened Runtime and the role-specific
   `config/macos-entitlements.json` minimum entitlements.
5. Submit for Apple notarization, wait for acceptance, staple, and verify.
6. Produce a DMG, install to `/Applications/AhoiBrowser.app`, and verify bundle
   identity, version, commit, architecture, signature, notarization, and hash.
7. Run all applicable CU_E2E and ASSISTED_E2E cases plus performance comparison.
8. Generate full and delta updates; test success, signature rejection, network
   interruption, disk-full behavior, atomic replacement, restart, and rollback
   prevention.
9. Archive debug symbols separately and generate canonical `SHA256SUMS` for the
   ZIP, DMG, symbols, SBOM, notices, licenses and source offer.
10. Complete the candidate-bound third-party and trademark review, then publish
    source, patches, release notes, update metadata and the evidence index.

## Implemented release tooling

`scripts/release/ahoi-release.py` implements the fail-closed release engineering
path without embedding credentials. Its receipts bind:

```text
schema-v2 ahoi-release build provenance
  -> unsigned exact bundle tree and main binary
  -> leaf-to-root nested Developer ID signatures
  -> accepted Apple app/DMG submissions and response logs
  -> stapled app tree and stapled DMG
  -> ZIP/DMG bytes
  -> /Applications/AhoiBrowser.app tree and signing identity
  -> SPDX SBOM, notices, licenses and corresponding-source offer
  -> separate deterministic debug-symbol archive and canonical SHA256SUMS
  -> canonical Ed25519-signed release manifest
```

The exact tree hash covers relative paths, entry types, permission bits,
symlink targets and file bytes. The independent validator also checks the
configured Chromium revision, release GN-argument hash, complete pinned
toolchain object, app/main-binary hashes, every receipt/file hash and the live
installed app's signature, Hardened Runtime, entitlements, Gatekeeper result and
staple. Matching plist strings alone are insufficient.

All evidence for one build is stored in one release directory, for example
`artifacts/releases/0.0.1-1/`. Copy the immutable
`artifacts/build/ahoi-release-build.json` into that directory before signing;
receipt references may not escape it. A normal directory contains the build,
signing, notarization, package, installed and materials receipts, both Apple
JSON response logs and their submitted archives, ZIP/DMG, SBOM, notices,
source offer and final `release-manifest.json`.

Run the CLI help for exact arguments:

```sh
python3 scripts/release/ahoi-release.py --help
python3 scripts/release/ahoi-release.py sign --help
python3 scripts/release/ahoi-release.py notarize-package --help
python3 scripts/release/ahoi-release.py materials --help
python3 scripts/release/ahoi-release.py release-assets --help
python3 scripts/release/ahoi-release.py install --help
python3 scripts/release/ahoi-release.py bind-installed --help
python3 scripts/release/ahoi-release.py assemble --help
python3 scripts/release/ahoi-release.py verify-chain --help
python3 scripts/release/ahoi-release.py sparkle-appcast --help
```

`sign` requires `AHOI_CODESIGN_IDENTITY` and `AHOI_TEAM_ID`. It signs Mach-O
leaves before nested `.app`/`.xpc`/framework containers and signs the outer app
last; every code object must match exactly one role in
`config/macos-entitlements.json`. It never uses `codesign --deep` to sign.
`--deep --strict` is used only as an additional verification pass.

`notarize-package` requires only the name of an existing Keychain profile in
`AHOI_NOTARY_KEYCHAIN_PROFILE`; secrets are never command arguments or receipt
fields. It retains the exact app-upload ZIP and pre-staple DMG plus Apple's
canonical response logs, requires both statuses to be `Accepted`, staples and
validates app and DMG, and runs Gatekeeper assessment before emitting receipts.

`materials` consumes an explicit component inventory and emits both an SPDX
SBOM and deterministic license-text archive. Every component requires
a resolved SPDX license expression and one or more in-tree license files. A
missing component, `NOASSERTION`, absent Third-Party Notices, or absent
corresponding-source offer blocks the release. Chromium's complete shipped
component inventory must be generated from the exact candidate; the small
`config/third-party-pins.json` file is not a substitute.
The production `materials` command additionally requires exactly one Sparkle
component matching the reviewed 2.9.6 version, release URL, MIT conclusion and
canonical in-repository license evidence. Appcast generation revalidates the
inventory, SPDX package, materials receipt and their hashes before signing.

`release-assets` recursively inventories all `.dSYM` bundles and standalone
`.sym` files below the exact release output, rejects symbol symlinks and empty
inputs, and writes a byte-reproducible ZIP with a per-member hash/mode
inventory. It then writes sorted GNU-compatible `SHA256SUMS` entries for every
packaged artifact, material artifact and the symbols ZIP. Its receipt binds the
package and materials receipts; the final signed manifest requires and
independently revalidates that receipt. Keep symbols outside the shipped app and
publish them only through the access-controlled crash-analysis archive.

Mount the real notarized DMG, quit every AhoiBrowser process, and install its
already verified app through the canonical transaction (use `sudo` when the
current account cannot write `/Applications`):

```sh
sudo python3 scripts/release/ahoi-release.py install \
  --app "/Volumes/AhoiBrowser/AhoiBrowser.app" \
  --signing-receipt "artifacts/releases/0.0.1-1/signed-package-provenance.json" \
  --notary-receipt "artifacts/releases/0.0.1-1/notarization-receipt.json" \
  --output "artifacts/releases/0.0.1-1/installed-bundle-binding.json"
```

`install` has no configurable destination: it accepts only a canonical,
non-symlink `AhoiBrowser.app` candidate and the policy-fixed
`/Applications/AhoiBrowser.app` target. Before any mutation it binds the exact
post-staple tree and bundle identity to the signing/notarization receipts and
repeats Developer ID, ARM64-only, Hardened Runtime, entitlement, Gatekeeper and
staple verification. It then locks installation, rejects running processes from
either bundle, copies into a uniquely version-bound hidden path on the
`/Applications` filesystem, and verifies the copy again.

For replacement, Darwin `renameatx_np(RENAME_SWAP)` exchanges the complete
target and staged candidate in one filesystem operation. The old app remains at
`/Applications/.AhoiBrowser.rollback-v<version>-b<build>-s<commit>-h<hash>.app`.
For first installation, `renameatx_np(RENAME_EXCL)` atomically publishes the
staged app only if the destination is still absent. Any subsequent installed-app
verification or receipt-write failure automatically reverses the exchange (or
restores target absence) before reporting failure. If even rollback cannot be
proven, the command stops with both exact recovery paths and never deletes an
unrecognized tree. `SIGKILL` and power loss cannot run a handler, but the atomic
filesystem operation still leaves a complete new target and, for replacement,
the complete version-bound old bundle.

`bind-installed` remains a read-only diagnostic for an app installed by some
other mechanism. It repeats the live checks but deliberately emits no atomic
installation evidence, so its receipt cannot satisfy `assemble`. The canonical
`install` receipt records same-volume staging, process quiescence, atomic method,
retained backup identity/hash, rollback policy and successful post-install
verification. `assemble` validates those relationships before signing canonical
JSON as release-manifest schema 2. Validation remains backward-compatible with
already signed schema-1 manifests, whose historical installed receipts predate
the atomic-transaction evidence. Re-running assembly with unchanged inputs and
key produces identical manifest bytes.

The separate `scripts/install-dev-app.py` command uses the same reviewed atomic
transaction and rollback implementation for an already stamped and signed
`AhoiDev` Computer Use candidate. Its fixed target is also
`/Applications/AhoiBrowser.app`, but it verifies with
`scripts/verify-built-app.sh` and emits a development-only receipt marked
`releaseEvidenceEligible=false`. It cannot consume release receipts, satisfy
`assemble`, or replace Developer ID/notarization verification.

## AhoiBrowser Mobile release boundary

The Mobile target has a separate Apple distribution chain; a desktop release
receipt cannot satisfy it. `apps/AhoiMobile/AhoiMobile.Release.xcconfig.template`
points Release signing at
`AhoiMobile.DefaultBrowser.entitlements.template`, which intentionally cannot
be materialized honestly until Apple grants the managed default-browser
entitlement to the final bundle ID and the Team provisions matching CloudKit,
push and Keychain capabilities.

### Development readiness snapshot — 2026-08-30

The current Mobile state has bounded development evidence, not a distribution
candidate:

- the Debug iPhone simulator build succeeded;
- the Release iPad simulator build succeeded for `arm64` and `x86_64`;
- 56 Mobile Core test cases were reported with zero failures and two
  entitlement-dependent skips;
- all three Mobile UI tests passed;
- all 36 CloudKit/security package tests passed without constituting a real
  cloud mutation;
- a development build was signed and installed on an iPhone 16 Pro Max running
  iOS 26.6; after an initial lock-related CLI launch rejection, a visible smoke
  through the host's iPhone device-management/synchronization flow proved cold
  start, HTTPS `example.com`, Browser Actions, a new private tab and normal-tab
  restore with the private tab excluded after targeted Ahoi process termination;
  this path was unrelated to Ahoi CloudKit sync and remains bounded development
  evidence, not a journey pass; and
- the available iPad (6th generation) runs iPadOS 17.7.10 and is incompatible
  with the Mobile target's iOS/iPadOS 26 deployment floor.

Every `MOB-USER-*` and `IOS-*` registry entry therefore remains `NOT_RUN`.
The retained screenshots are indexed under
[`audit-evidence/2026-08-30-ios-device-preflight/`](audit-evidence/2026-08-30-ios-device-preflight/).
There is no distribution certificate/profile, App Store Connect API key or
processed TestFlight build. Apple has not granted the managed default-browser
entitlement. No real CloudKit container or AES/Ed25519 keys are configured, no
operational provisioning/rotation/revocation path has been demonstrated, and
the Mac development app is not an entitled CloudKit counterparty.

### Mobile release preflight

`apps/AhoiMobile/scripts/release-preflight.sh` is wired into the Mobile target
as a pre-build phase. Debug validates the tracked source contract. Release also
fails closed unless Team, bundle/test identifiers, version/build, CloudKit,
Keychain, profile and export-compliance values are concrete. The export-
compliance Boolean is an explicit legal/release input; the repository does not
decide whether the exact candidate is exempt.

Materialize private copies of `AhoiMobile.Release.xcconfig.template` and
`ExportOptions.plist.template` outside version control, replace every sentinel
with the candidate's reviewed value, regenerate the tracked project and create
the archive with the private xcconfig explicitly attached:

```sh
cd apps/AhoiMobile
xcodegen generate --spec project.yml

xcodebuild \
  -project AhoiMobile.xcodeproj \
  -scheme AhoiMobile \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath /private/path/AhoiMobile.xcarchive \
  -xcconfig /private/path/AhoiMobile.Release.xcconfig \
  archive
cd ../..
```

The tracked Release template enforces manual Apple Distribution signing. Do not
add either materialized file to Git. Bind the archive inspection to the same
reviewed candidate values before exporting:

```sh
env \
  AHOI_APP_USES_NON_EXEMPT_ENCRYPTION=YES_OR_NO \
  AHOI_CLOUDKIT_CONTAINER_ID=iCloud.example.replace-me \
  AHOI_SYNC_KEYCHAIN_ACCESS_GROUP=TEAMID.example.sync \
  AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP=TEAMID.example.commands \
  AHOI_MOBILE_BUNDLE_ID=example.replace-me \
  AHOI_MOBILE_MARKETING_VERSION=1.0 \
  AHOI_MOBILE_BUILD_NUMBER=1 \
  apps/AhoiMobile/scripts/release-preflight.sh --archive \
  /path/to/AhoiMobile.xcarchive \
  --export-options /path/to/ExportOptions.plist

xcodebuild \
  -exportArchive \
  -archivePath /path/to/AhoiMobile.xcarchive \
  -exportOptionsPlist /path/to/ExportOptions.plist \
  -exportPath /private/path/AhoiMobile-export
```

The archive check is read-only. It verifies version/build, Boolean export
declaration, distribution profile expiry, Team/bundle binding, managed default-
browser entitlement, production CloudKit/push environment, both Keychain
groups, Privacy Manifests, signature and opaque compiled AppIcon renditions
before export. The tracked 1024-point icon is now RGB without an alpha channel;
the conversion changed no visible pixel values.

A Mobile candidate requires all of the following, bound to one source commit and
build number:

1. final Team ID, App ID/bundle ID, development/distribution certificates and
   profiles, including the Apple-managed default-browser entitlement;
2. concrete private CloudKit container/environment and reviewed synchronizable
   Keychain groups/keys without repository-held private material;
3. generated Xcode project inputs, Release archive and exported IPA with exact
   signature/profile/entitlement inspection;
4. merged Privacy Manifest plus embedded-SDK manifests reconciled against actual
   endpoints, Apple's collection definitions and App Store Connect privacy
   disclosures; the current conservative source manifests declare no tracking
   while listing browsing history, search history, other user content and
   device ID as unlinked, non-tracking app-functionality data;
5. the exact signed candidate installed on supported physical iPhone and iPad,
   followed by `MOB-USER-01` through `MOB-USER-15` and `IOS-01` through
   `IOS-15` with retained evidence;
6. entitled Mac–Mobile CloudKit/Keychain convergence, offline, conflict,
   account/zone recovery, key rotation/revocation and private-data exclusion;
7. App Store Connect archive upload and processing receipts, tester access and
   installation/launch of the processed TestFlight build on both form factors.

The current worktree supplies source templates, policy seams and the bounded
development evidence above. It records simulator builds/tests and one signed
physical development installation/smoke, but not a complete physical journey,
distribution archive/export, entitled roundtrip, App Store processing result or
TestFlight installation. Granular ownership and states live in
`config/external-gates.json`.

## Gates intentionally still closed

The implementation does not manufacture production evidence.
`config/release-policy.json` therefore contains no trusted production manifest
or feed key IDs, and `config/release-evidence.json` remains
`releasePassEnabled=false`. The following are still real release blockers:

- provision Developer ID and Notary Keychain access and complete both Apple
  submissions;
- independently pin the production manifest public key, plus a distinct key
  for each update channel, in `config/release-policy.json`;
- produce a complete Chromium component inventory, notices and corresponding
  source package and complete the review contract in
  `docs/THIRD_PARTY_REVIEW.md`;
- run the canonical install transaction from the resulting DMG and retain its
  installed-bundle receipt plus any version-bound rollback bundle;
- host the signed metadata/artifacts over HTTPS and pass real N-2/N-1, failed
  download, disk-full, full fallback and any enabled delta-update journeys;
- pass the remaining release-critical installed CU/ASSISTED test matrix.

Only after actual receipts exist and the independent live validator succeeds
may a separate reviewed change consider enabling release evidence. The mere
presence or unit-test success of this tooling is not that evidence.

## Hard release gates

A release is blocked by a known sandbox/site-isolation weakening, an unreviewed
secret path, missing Chromium security update, failed signature/notarization,
non-PASS release-critical test, silent network endpoint, broken updater recovery,
unresolved licensing obligation, missing signed release-provenance chain, or a
fake/partial implementation presented as complete.

The update metadata and full-package transaction are documented in
`docs/UPDATES.md`. Official Sparkle 2.9.6 remains the sole native
check/download/install/relaunch engine for updates. The repository CLI above is
only the explicit initial/manual release installation and evidence transaction;
it does not download, extract, relaunch or replace Sparkle. The signed appcast
provenance receipt binds its exact bytes to the signed release manifest and
materials receipt. Update private keys and notarization credentials live only
in secured release infrastructure or Keychain-backed local configuration.
