# Releasing

Channels are `dogfood`, `beta`, and `stable`. Every channel uses signed update
metadata and an independently verifiable application signature. Stable is never
promoted solely from CI; installed-app E2E evidence and the release checklist
are required.

`config/version.json` separates the numeric Apple marketing version and
monotonic build number from the dogfood/beta/stable channel and the human-facing
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

The current Phase-0 repository does not yet implement the signed provenance
chain between steps 2, 4, 5, and 6. `config/release-evidence.json` consequently
disables Computer Use PASS. Enabling it requires a validator that binds the
unsigned build manifest to the signed/package artifact, Apple notarization
receipt, and exact installed bundle hash; changing the flag without that
implementation is not a release procedure.

## Hard release gates

A release is blocked by a known sandbox/site-isolation weakening, an unreviewed
secret path, missing Chromium security update, failed signature/notarization,
non-PASS release-critical test, silent network endpoint, broken updater recovery,
unresolved licensing obligation, missing signed release-provenance chain, or a
fake/partial implementation presented as complete.

Sparkle 2 is the preferred updater candidate, subject to final integration and
licensing review. Update keys and notarization credentials live only in secured
release infrastructure or Keychain-backed local configuration.
