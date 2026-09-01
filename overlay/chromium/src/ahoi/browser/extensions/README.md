# AhoiBrowser extension compatibility and uBlock Origin Classic

This directory contains the only AhoiBrowser exception to Chromium's Manifest
V2 retirement. It is deliberately a supply-chain policy, not a general MV2
feature flag.

## Compatibility boundary

Chromium continues to own ordinary extension installation and execution. This
code does not special-case extension actions, popups, options pages, service
workers, content scripts, permissions, native messaging, or extension UI.
Manifest V3 extensions and all non-uBO extensions therefore keep Chromium's
normal behavior and security model. The feature grants no extension privileged
access to AhoiBrowser's own UI.

The MV2 handler's manifest-only installation preflight remains fail-closed. The
embedder exception is consulted only after a complete CRX has been verified and
materialized as an `Extension` object. The exception accepts all of the
following or nothing:

- the compile-time `enable_ahoi_ubo_classic` gate, enabled by default and in
  the dev, full-dev, release, and full-release desktop profiles;
- internal location, ordinary extension type, and manifest version 2;
- fixed ID `fkgkibajhfbepljeaefdnfnegdcjomkh`;
- exact authorized version, complete-CRX SHA-256, and CRX signing-public-key
  SHA-256;
- no package-controlled `update_url`;
- either the currently verified installation transaction or atomically
  committed, local-only authorization state.

Foreign, unpacked, repackaged, differently signed, or merely ID-spoofed MV2
packages remain blocked. Authorization is intentionally not a syncable pref.
The authorization schema migration invalidates state for the former identity;
no package is silently carried across the identity change. A build with the
compile-time gate disabled hides every uBO product entry point, rejects the MV2
exception, clears stale local authorization, and leaves an installed Classic
copy disabled rather than silently uninstalling it.

## Initial product-bootstrap trust root

`ubo_product_config.cc` is the single production trust root. The signed browser
contains one static initial-install entry for the **Official GitHub release**:

```text
version                    1.74.0
upstream tag               1.74.0
upstream commit            6dd2d95e50d134a477a4e183343c0b26e9147123
extension ID               fkgkibajhfbepljeaefdnfnegdcjomkh
CRX SHA-256                b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e
CRX public-key SHA-256     5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf
release page               https://github.com/gorhill/uBlock/releases/tag/1.74.0
package URL                https://github.com/gorhill/uBlock/releases/download/1.74.0/uBlock0_1.74.0.chromium.crx
license                    GPL-3.0-or-later
```

A manual initial check constructs that exact entry locally. It performs no
catalog request. The package request starts only at the exact URL above, omits
credentials, disables cache, and accepts at most one redirect: HTTP 302 with a
GET to host `release-assets.githubusercontent.com`, no credentials, port, or
fragment, and exact immutable path
`/github-production-release-asset/33263118/ade4daf2-50e8-4953-8821-5c2d43f07a65`.
The signed query must be present. Authorization and cookie headers are removed
at the boundary. A second redirect, another host/path, another method/status,
or a changed initial URL fails closed.

The redirect does not establish package trust. After download the verifier
requires the SHA-256 of the complete CRX, verifies its CRX3 proof with the
required public-key hash, derives the fixed extension ID from that key, and
checks the manifest version. `SandboxedUnpacker` enforces the package hash
again; its opt-in flag defaults to false for every existing Chromium caller.

## Signed catalog for later updates

The existing signed-catalog path remains separate and is intentionally
unprovisioned in production until its hosting and Ed25519 trust root pass the
release gate. Its envelope has exactly two keys:

```json
{"payload":"<exact JSON bytes>","signature":"<base64 Ed25519 signature>"}
```

Schema 2 has exactly these 14 payload keys:

```text
schema_version              integer, exactly 2
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
upstream_commit             full 40-character lowercase commit
upstream_source_url         exact GitHub release/tag URL
license                     GPL-3.0-or-later
```

The verifier checks the Ed25519 signature over the exact payload bytes before
parsing. It rejects extra fields, oversized or expired catalogs, origin
changes, bad hashes, non-uBO IDs, and invalid provenance. Sequence and extension
version are monotonic. A lower sequence or version is rejected; reusing a
version with a different package hash is rejected even at a higher sequence.
The first signed update must use a sequence greater than the static bootstrap
sequence `174000`.

Periodic checks exist only after the fixed extension is installed and locally
authorized and only when the signed catalog is provisioned. They fetch catalog
metadata, expose an available update in the manual dialog, and never download
or install a package. Uninstalling stops the timer, deletes any temporary
candidate, and clears local authorization.

## Native install surface and authorization

`UboServiceFactory` owns one service per regular profile and none for
off-the-record profiles. Requests use Chromium's browser-process
`URLLoaderFactory` with credentials omitted, cache disabled, a 30-second
timeout, a 64-KiB catalog limit, and a 32-MiB package limit. The CRX lives in a
temporary file that is deleted after rejection, cancellation, shutdown, or
installer completion. The service owns a cancellable install operation until a
terminal result; the operation observes profile destruction, uses weak
callbacks into Chromium's installer, and retains source-file cleanup even when
the installer produces no callback.

The Extensions submenu opens a native, browser-modal surface. For the initial
entry it visibly names **Official GitHub release** and shows the version, fixed
ID, full upstream commit and release URL, complete package hash, and GPL license
before download. The only progression is:

```text
manual check -> browser-pinned entry -> explicit download
             -> hash + key + ID + CRX3 verification -> explicit install
             -> Chromium permission prompt -> atomic authorization commit
```

Opening the dialog, checking, or downloading never installs or enables an
extension. Installation is rejected unless requested from an active tab in the
same regular profile. Once verification completes, the Ahoi sheet closes and
detaches before a posted task creates Chromium's standard permission prompt;
destruction of the sheet cannot cancel that accepted handoff. Chromium's prompt
remains authoritative. Authorization is committed only after the installed
extension matches the verified metadata. If the final state write fails after
a fresh install, the normal Chromium uninstall path removes the new extension
and waits for its file/site-data cleanup before reporting failure. A previously
installed copy is never silently removed; its old authorization is restored
when possible and it stays disabled if rollback cannot be completed safely.
There is no preinstallation, silent update, automatic enablement, broad MV2
allowlist, or unpacked-install exception.

## Release verification procedure

No uBlock source tree, CRX, private key, filter list, logo, or other third-party
asset is copied into this repository. For the pinned artifact a release
operator must retain an attestation containing:

1. the exact Official GitHub release page, tag, full release commit, package
   URL, response chain, retrieval time, and downloaded byte size;
2. the SHA-256 of the complete downloaded CRX;
3. the DER bytes and SHA-256 of the CRX3 `sha256_with_rsa` public key;
4. the extension ID derived from the first 128 bits of that key hash using
   Chromium's nibble-to-`a`..`p` mapping;
5. independent CRX3 verification plus extracted manifest version and absence
   of a package-controlled `update_url`;
6. `ahoi_extension_policy_unittests`, `ahoi_extension_ui_unittests`, focused
   redirect/package tests, and installed-app extension regression tests.

Never rebuild, repack, or re-sign while claiming these pins. Any changed
version, commit, package bytes, public key, ID, release asset object, or URL is a
new product-security migration requiring explicit review and new browser pins.

Focused release contracts are UBO-01 for the explicit initial install hand-off,
UBO-02 for exact provenance/hash/key/ID verification, UBO-09 for a later signed
catalog update and rollback rejection, UBO-11 for rejection of foreign MV2, and
UBO-13 for builds with neither bootstrap nor signed-catalog trust roots. UBO-10
remains the uninstall journey. The unpacked MV2 fixture is always a negative
control and never positive uBO evidence.

## External release gates

The static pins make the bootstrap package technically verifiable; they do not by
themselves grant public redistribution rights. Public release still requires
source/license/GPL, name/logo, redistribution, provenance-retention, Release QA,
and rollback review.

Later updates additionally require an owned HTTPS catalog/artifact origin, an
immutable publishing workflow, and an offline/HSM Ed25519 catalog key with only
its public key embedded in AhoiBrowser. These dependencies remain tracked in
`config/external-gates.json`. Passing the initial package-verification gate does
not provision the future network catalog, and provisioning the catalog does not
waive the redistribution review.
