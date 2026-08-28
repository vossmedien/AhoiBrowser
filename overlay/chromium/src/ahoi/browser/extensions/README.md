# AhoiBrowser extension compatibility and uBlock Origin Classic

This directory contains the only AhoiBrowser exception to Chromium's Manifest
V2 retirement. It is deliberately a supply-chain policy, not a general MV2
feature flag.

## Compatibility boundary

Chromium continues to own ordinary extension installation and execution. This
code does not special-case extension actions, popups, options pages, service
workers, content scripts, permissions, native messaging, or extension UI.
Manifest V3 extensions and all non-uBO extensions therefore keep Chromium's
normal behavior and security model. In particular, this feature grants no
extension privileged access to AhoiBrowser's own UI.

The MV2 handler's manifest-only installation preflight also remains unchanged
and fail-closed. The embedder exception is consulted only after a complete CRX
has been verified and materialized as an `Extension` object. The exception
accepts all of the following or nothing:

- internal location, ordinary extension type, and manifest version 2;
- fixed ID `cjpalhdlnbpafiamejdnhcphjbkeiagm`;
- exact catalog version and CRX signing-public-key SHA-256;
- no package-controlled `update_url`;
- either the currently verified installation transaction or atomically
  committed, local-only authorization state.

Foreign, unpacked, repackaged, differently signed, or merely ID-spoofed MV2
packages remain blocked. Authorization is intentionally not a syncable pref.

## Trust chain

`ubo_product_config.cc` is the single production trust root. It pins the exact
catalog URL, artifact origin, fixed extension ID, and Ed25519 catalog public
key. The checked-in configuration is intentionally unprovisioned and thus
cannot install anything.

The signed catalog envelope has exactly two keys:

```json
{"payload":"<exact JSON bytes>","signature":"<base64 Ed25519 signature>"}
```

The payload has exactly these 13 keys:

```text
schema_version              integer, exactly 1
sequence                    decimal uint64 string, nonzero
valid_from                  Unix seconds as decimal string
valid_until                 Unix seconds as decimal string, at most 45 days
extension_id                fixed Ahoi allowlist ID
version                     valid Chromium extension version
package_url                 HTTPS, pinned artifact origin
update_manifest_url         HTTPS, pinned artifact origin
sha256                      lowercase SHA-256 of the complete CRX
crx_public_key_sha256       lowercase SHA-256 of the CRX signing public key
upstream_tag                exact uBlock release tag
upstream_source_url         exact GitHub release/tag URL
license                     GPL-3.0-or-later
```

The verifier checks the signature over the exact payload bytes before parsing
them. It rejects extra fields, oversized or expired catalogs, origin changes,
bad hashes, non-uBO IDs, and invalid provenance. Package verification then
uses Chromium's CRX3 verifier with both the full-file hash and required signing
key hash pinned. `SandboxedUnpacker` is instructed to enforce the hash again;
the new opt-in flag defaults to false for every existing Chromium caller.

The catalog sequence and extension version are monotonic. A lower sequence or
version is rejected. Reusing a version with a different package hash is also
rejected, even with a higher sequence. Chromium's `CrxInstaller` provides its
normal permission prompt and atomic on-disk installation. Authorization is
committed only after the installed extension matches the verified metadata. If
that final state write fails, the new MV2 extension is immediately disabled.

## Profile service and native install surface

`UboServiceFactory` eagerly owns exactly one service for each regular profile.
It creates no service for off-the-record profiles. The service uses Chromium's
browser-process `URLLoaderFactory` with credentials omitted, cache disabled, a
30-second timeout, a 64-KiB catalog limit, and a 32-MiB package limit. Both
requests require an exact HTTPS URL. Any redirect, final-URL change, HTTP or
network failure, or oversize response fails closed. The CRX is held in a
temporary file and deleted after rejection, cancellation, shutdown, or the
installer hand-off.

The Extensions submenu opens a native, browser-modal surface. It shows the
catalog version, fixed extension ID, upstream release tag and source, package
SHA-256, and GPL license before the user can download. The only progression is:

```text
explicit check -> signed catalog ready -> explicit download
               -> package verified -> explicit install
               -> Chromium permission prompt -> atomic authorization commit
```

Opening the dialog, checking, or downloading never installs or enables an
extension. The install action is rejected unless it comes from an active tab in
the same regular profile. The standard Chromium prompt remains authoritative.
No Ahoi UI privilege is added to the installed extension.

An optional 24-hour timer exists only while the fixed extension is both
installed and locally authorized. It fetches and verifies only the signed
catalog, exposing an update in the same manual dialog. It never downloads a
package or starts installation. Uninstalling the fixed extension stops the
timer, removes any temporary candidate, and clears local authorization.

## Reproducible release procedure

No uBlock source tree, CRX, private key, filter list, logo, or other third-party
asset is copied into this repository. A release operator must perform these
steps in a clean, hermetic builder and retain the resulting attestation:

1. Fetch `https://github.com/gorhill/uBlock` and check out the exact catalog
   tag and commit. Verify the tag/commit according to the project's published
   release process. Record the remote URL, tag, commit, builder image digest,
   toolchain versions, and submodule state.
2. Run the upstream `tools/make-chromium.sh <version>` from that checkout. Run
   the same clean build twice and require identical unpacked-file hashes and
   ZIP bytes. Any required normalization must be documented in the builder
   recipe, not patched into this repository.
3. Package the result as CRX3 with the approved publisher key. Record
   `sha256sum <package.crx>`. Use Chromium's CRX verifier to extract the
   signing public key and require both its SHA-256 and the derived extension ID
   to match the release allowlist.
4. Upload the immutable, hash-addressed CRX and update manifest to the pinned
   HTTPS artifact origin. Do not publish redirects to another origin. Stage
   both objects before publishing the catalog.
5. Generate the 13-field payload above. Have an offline signer or HSM sign the
   exact UTF-8 payload bytes with Ed25519. Store no signing secret in source,
   build arguments, CI logs, catalog, or the browser bundle.
6. Verify the finished envelope with the public key embedded in the intended
   AhoiBrowser build, then atomically publish it. Retain the previous immutable
   artifacts for operational rollback; never reduce catalog sequence or
   extension version. A bad new release is rolled forward with a greater
   sequence and version.
7. Run `ahoi_extension_policy_unittests`,
   `ahoi_extension_ui_unittests`, plus the Chromium extension browser
   tests covering actions/popups, service workers, content scripts,
   permissions, native messaging, and MV3 before release.

The native surface invokes `InstallUboPackageFromVerifiedCatalog` only after
the explicit sequence above. There is no preinstallation, silent update, or
automatic enablement.

Focused test contracts are named for release traceability: UBO-01 covers the
complete explicit verified install hand-off, UBO-02 covers catalog-only
periodic checks, UBO-09 covers rollback rejection before package download, and
UBO-13 covers the unprovisioned fail-closed product state. UBO-10 remains the
uninstall journey defined by the product test registry. Additional tests
cover foreign MV2 and unchanged MV3 behavior, catalog tampering, redirected,
oversized and offline fetches, package hash failure, and uninstall cleanup.

## External release gate

The public ID above is the Chrome Web Store identity documented by uBlock
Origin upstream. Rebuilding source does not provide the private key that owns
that identity. Shipping with this ID therefore requires an authentic CRX whose
signing key derives that exact ID and whose redistribution provenance and GPL
obligations are approved.

Before enabling the production configuration, AhoiBrowser still needs:

- an owned HTTPS catalog/artifact origin and immutable publishing workflow;
- an offline/HSM Ed25519 catalog key, with only its public key checked in;
- proof of authorization to distribute a CRX signed by the key for the fixed
  ID, plus recorded upstream/tag/build provenance;
- release QA and retained reproducible-build/signing attestations.

These dependencies are tracked independently in
`config/external-gates.json` as `ubo-catalog-hosting-and-signing`,
`ubo-fixed-id-crx-publisher-provenance`, and `ubo-redistribution`. Passing one
gate never substitutes for either of the others.

If AhoiBrowser instead uses its own CRX publisher key, the derived extension ID
will be different. That is a deliberate product/security migration: change the
central fixed ID, document that the package is Ahoi-built from upstream source,
and re-review all trust roots and tests. Never claim the Web Store identity for
a CRX signed by another key, and never weaken the ID/key checks to make a
repackaged bundle install.

Until those gates are complete, `GetProductionUboProductConfig()` remains
unprovisioned and the whole path fails closed.
