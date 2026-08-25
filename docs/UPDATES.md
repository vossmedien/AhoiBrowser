# Native macOS updates

AhoiBrowser uses the official Sparkle 2.9.6 binary distribution as
its only runtime update engine. There is no repository-owned archive extractor,
installer, privileged helper, relauncher, or binary-delta implementation.
Sparkle owns check, download, Ed25519 verification, extraction, installation,
full fallback, and relaunch.

## Security baseline

The reviewed dependency contract is recorded in
`config/third-party-pins.json` and duplicated in the release policy:

- version `2.9.6`;
- source commit `ac2def288cbff5cfc7df3ffef6abdf45b72bcb0a`;
- official `Sparkle-2.9.6.tar.xz` SHA-256
  `52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192`;
- MIT license plus the upstream bundled notices in
  `overlay/chromium/src/third_party/sparkle/LICENSE`, byte-pinned by SHA-256
  `389a4e4e9a32f059775b13a06e25a591445ba229d2838d26dd3e7c0c45127cfe`.

2.9.6 is the minimum permitted pin. Sparkle 2.9.5 and older are affected by
high-severity advisories
[GHSA-3x7w-j75x-ppq5](https://github.com/sparkle-project/Sparkle/security/advisories/GHSA-3x7w-j75x-ppq5)
and
[GHSA-4v99-qgq9-6pxp](https://github.com/sparkle-project/Sparkle/security/advisories/GHSA-4v99-qgq9-6pxp).
The pin validator requires the fixed version, exact upstream commit, artifact
hash, and both advisory IDs. A local patch or source rebuild is not an accepted
substitute.

Every enabled runtime requires all of the following and otherwise stays
unavailable without making a request:

- a credential-free HTTPS `SUFeedURL`;
- an Ed25519 `SUPublicEDKey` that decodes to exactly 32 bytes;
- the bundled Sparkle framework version matching the reviewed pin;
- `SURequireSignedFeed=true` and `SUVerifyUpdateBeforeExtraction=true`;
- `SUSendProfileInfo=false`, no feed parameters, and no profile keys.

Release stamping additionally requires the feed URL and public key to match the
independently reviewed values for that channel in
`config/release-policy.json`. The repository intentionally contains empty
production values today. Do not replace them with examples or secrets.

## Native browser surface and channels

The macOS application menu contains **Check for Updates...** and
**Software Updates...**. The settings alert exposes current status and channel,
automatic-check/download preferences, and a manual check. Sparkle's standard
native user driver owns update discovery, progress, install consent, errors and
relaunch. Status changes are announced through macOS accessibility APIs. Ahoi
strings are available in German, English and British English.

Channels are immutable properties of a signed build:

- `stable` sees only Sparkle's untagged default channel;
- `beta` sees default plus `beta`;
- `nightly` sees default plus `beta` and `nightly`.

Each channel has its own reviewed feed/artifact-base/public-key tuple. Switching
a binary to another channel at runtime is intentionally not supported because
it would also change that trust tuple. Promotion publishes the same verified
artifact into a more restrictive signed appcast; it does not rebuild or mutate
the artifact.

## Fetching, bundling and signing

`scripts/fetch-sparkle.sh` downloads only the pinned official HTTPS archive,
verifies its SHA-256 before extraction, installs `Sparkle.framework` plus the
official `generate_appcast` and `sign_update` tools, and retains only arm64
slices for the arm64-only product. The generated framework directory is ignored
by Git and can always be reproduced from the pin.

Development ad-hoc signatures only make the thinned dependency locally usable.
The release signer signs each Sparkle Mach-O/XPC/app leaf, the framework, the
Chromium framework/helpers and the outer application leaf-to-root with Hardened
Runtime and a trusted timestamp. It never uses `codesign --deep` to sign;
`--deep --strict` is verification only. AhoiBrowser is not App Sandbox enabled,
so Sparkle's sandbox-specific downloader/installer service flags are not set.
This follows Sparkle's
[sandboxing guidance](https://sparkle-project.org/documentation/sandboxing/).

## Signed appcast and provenance contract

After the notarized ZIP/DMG, release manifest, materials receipt and complete
SPDX/component-license evidence exist, generate an appcast with the official
tool through the release CLI:

```sh
export AHOI_SPARKLE_KEY_ACCOUNT='reviewed-keychain-account'
python3 scripts/release/ahoi-release.py sparkle-appcast \
  --channel nightly \
  --archives artifacts/releases/0.0.2-2/updates \
  --output-name appcast-nightly.xml \
  --minimum-update-version 1 \
  --expected-build 2 \
  --release-manifest artifacts/releases/0.0.2-2/release-manifest.json \
  --manifest-public-key /secure/path/release-manifest-public.pem \
  --materials-receipt artifacts/releases/0.0.2-2/materials.json \
  --receipt artifacts/releases/0.0.2-2/appcast-nightly-provenance.json
```

The command reads the channel feed/artifact URLs and public key only from the
reviewed policy and
the private Ed25519 key only from the named Keychain account. It invokes
Sparkle's official delta/full appcast generator, verifies the resulting signed
feed again with official `sign_update`, then enforces the repository contract:

- the signed-feed trailer is the final content and its signed length binds the
  complete XML prefix;
- every enclosure is credential-free HTTPS, has a 64-byte Ed25519 signature,
  positive length and positive numeric `CFBundleVersion`;
- the expected release build is present and no foreign channel item is present;
- the receipt records the exact appcast hash and the Sparkle version, commit and
  archive hash, reviewed feed/artifact URLs and public-key hash, and binds them
  to the signed release-manifest and materials receipt hashes.

The one-way appcast receipt avoids a circular hash while preserving a complete
auditable chain. Sparkle must also appear in the exact-candidate component
inventory, SPDX SBOM and license archive; the small pin file is not an SBOM.

## Rollback, recovery and release gates

Downgrade resistance comes from monotonically increasing numeric
`CFBundleVersion`, Sparkle's version checks, signed-feed verification, channel
isolation and signed minimum-update constraints. A delta is an optimization;
the matching signed full archive remains available for Sparkle fallback. Atomic
replacement and recovery are Sparkle behavior, not a second Ahoi installer.
Recovery to the pre-update bundle after a failed install is distinct from
publishing or accepting an older signed version.

Production release remains fail-closed until real values and evidence exist:

- reviewed HTTPS feeds and independent Ed25519 public keys for all published
  channels, with private keys held only in release Keychain/infrastructure;
- exact Developer ID signing, Apple notarization/stapling and installed-bundle
  verification for the Sparkle-bearing app;
- real N-2 and N-1 full/delta/fallback journeys plus tamper, downgrade, foreign
  channel, offline/interrupted network, disk-full and relaunch coverage;
- an HTTPS publication transaction that uploads immutable artifacts first,
  verifies their hashes, publishes the signed appcast last, and can restore the
  prior appcast atomically;
- complete SBOM, notices, source offer, legal review and release evidence.

Unit tests and structural validation prove fail-closed contracts; they are not
installed-app or production-feed evidence.
