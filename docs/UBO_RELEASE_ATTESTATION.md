# uBlock Origin Classic release attestation

`tools/ubo_release_attestation.py` creates the retained technical provenance
receipt required by UBO-01 and UBO-02. It has no configurable package URL and
does not accept a pre-downloaded or repacked CRX as positive evidence.

The tool performs exactly two credentialless, cache-disabled HTTPS GETs. The
first must be the pinned Official GitHub release URL and return exactly one HTTP
302 `Location`. The second must be the public transient
`release-assets.githubusercontent.com` URL at the exact pinned asset path, with
a signed query, and must return HTTP 200 without another redirect. The complete
file must match the pinned size and SHA-256.

It then parses the bounded CRX3 protobuf without generated or third-party
protobuf code, verifies every RSA and ECDSA proof independently with a
root-owned OpenSSL binary, checks the X.509 SPKI algorithm, retains the exact
developer-key DER as Base64 plus its hash, derives Chromium's extension ID, and
reads `manifest.json` directly from the bounded ZIP payload. Positive evidence
requires Manifest V2, version 1.74.0, and no package-controlled `update_url`.

The same receipt is bound to the reviewed release commit, clean Ahoi source
SHA, the combined overlay/patch fingerprint, separate patch-series and overlay
tree fingerprints, the legacy files-only bundle hash, the exact bundle-tree
hash, and the matching atomic installation receipt. The installation binding
uses the exact tree hash, which includes entry types, modes, symlink targets,
directories, and file bytes. Absolute local receipt paths, cookies, credentials,
private profile data, and CRX bytes are not retained. The public signed final
asset URL is retained exactly so the one-hop response chain remains auditable;
the authorization is transient and is not derived from a user account.

Run only after a clean `ahoi-dev` provenance receipt and its exact installation
receipt exist:

```sh
python3 tools/ubo_release_attestation.py \
  --build-provenance artifacts/build/ahoi-dev-build.json \
  --install-receipt artifacts/build/installed-ahoi-dev-<candidate>.json \
  --output artifacts/e2e/ubo-1.74.0-release-attestation.json
```

The output path must be new, have an existing canonical parent directory, and
remain below the canonical repository. Publication is atomic and refuses
overwrite. A failed redirect, TLS request, size/hash pin,
CRX3 proof, DER/key/ID check, manifest check, source fingerprint, or candidate
binding produces no positive receipt. The downloaded CRX exists only inside a
private temporary directory and is removed on success or failure.

This receipt proves the dogfood artifact's technical provenance. It does not
approve public redistribution, close GPL/name/logo review, provision the future
signed update catalog, prove the Chromium permission prompt, or replace the
visible installed-app filtering/restart/uninstall journeys.
