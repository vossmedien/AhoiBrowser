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
9. Publish source, patches, license notices, release notes, checksums, update
   metadata, and evidence index for the exact binary.

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

After installing through the real DMG, `bind-installed` accepts only
`/Applications/AhoiBrowser.app` and independently repeats signing,
architecture/identity, entitlement, Gatekeeper and staple checks. `assemble`
then validates every relationship before signing canonical JSON. Re-running it
with unchanged inputs and key produces identical manifest bytes.

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
  source package and complete legal review;
- install the resulting DMG and generate the installed-bundle receipt;
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
`docs/UPDATES.md`. Official Sparkle 2.9.6 is the sole native
check/download/install/relaunch engine; the former repository-owned installer
path has been removed. The signed appcast provenance receipt binds its exact
bytes to the signed release manifest and materials receipt. Update private keys
and notarization credentials live only in secured release infrastructure or
Keychain-backed local configuration.
