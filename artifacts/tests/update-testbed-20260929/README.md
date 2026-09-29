# Local Sparkle appcast and tamper testbed (DoD 15 "Updates", agent part)

Date: 2026-09-29. Host: macOS 27.0, OpenSSL 3.6.3. Source base: `288ad869`
plus the testbed commit that adds this directory.

This is repository and tool evidence only. It is **not** installed-app, real-feed
or release evidence (see "Not proven" below).

## What ran

The pinned official Sparkle 2.9.6 tools ran with a throwaway Ed25519 key:

- `generate_appcast`, SHA-256 `b3b54ba3…c6b7`
- `sign_update`, SHA-256 `bfb52400…34ad`

Both hashes are checked against `.work/state/sparkle/2.9.6/fetch-receipt.json`
(written by `scripts/fetch-sparkle.sh`) and the reviewed pin before any
scenario runs. Two stand-in `AhoiBrowser.app` bundles are about 3 KB each. Each
has only `Info.plist` and a non-executable stub; they are never launched. Their
`CFBundleVersion` values are 12 (N-1) and 13 (candidate), and they are zipped
with `ditto`. `generate_appcast` produces the signed nightly feed, both full
archives and one 13-from-12 delta.

Safety properties of the run:

- **Key:** OpenSSL generates the key pair in a `TemporaryDirectory`. The private
  seed reaches Sparkle only through `--ed-key-file` and is deleted afterwards.
  The tool wrapper refuses any command that has `--account` or lacks
  `--ed-key-file`, so the Keychain (and any product key in it) is never read.
  The public key is also compared with every `publicEdKey` in
  `config/release-policy.json`, which are all empty today.
- **No network:** every URL uses the reserved `.invalid` TLD
  (`https://updates.ahoi-testbed.invalid/nightly/`). Nothing is uploaded or
  served, and no `http.server` was needed.
- **Isolated cache:** `HOME` is redirected into the temporary directory, so
  `generate_appcast`'s `~/Library/Caches/Sparkle_generate_appcast` stays there.
- **Nothing product-side touched:** no Chromium build, and no GUI, keyboard or
  mouse. `/Applications/AhoiBrowser.app` was not touched, and
  `out/AhoiDev/AhoiBrowser.app` was only read with `plutil`, never modified,
  signed or launched.

## Commands

```sh
python3 scripts/release/ahoi-update-testbed.py \
  --report artifacts/tests/update-testbed-20260929/report.json \
  > artifacts/tests/update-testbed-20260929/console.txt
# rc=0, "26/26 checks passed"

python3 -m unittest tests.repository.test_update_testbed tests.repository.test_release_sparkle -v
# Ran 15 tests, OK (the official-tool scenario test ran, not skipped)

python3 -m unittest tests.repository.test_update_testbed tests.repository.test_release_sparkle \
  tests.repository.test_release_pipeline tests.repository.test_release_assets \
  tests.repository.test_release_installation
# Ran 44 tests, OK
```

In a linked worktree, the tools are found through the Git common directory.
To name them explicitly, pass `--sparkle-tools <dir>`.

## Results (`report.json`, `console.txt`)

In the table, "Sparkle" means `sign_update --verify` and "OpenSSL" means an
independent `openssl pkeyutl -verify -rawin` over the same bytes. "Contract"
means `tools/release/sparkle.py:validate_appcast_contract`. "Selection" means a
mirror of Sparkle's best-item choice: channel filter per `update_channel.cc`
and only strictly newer `CFBundleVersion`.

| ID | Scenario | Expected | Observed |
|---|---|---|---|
| A1 | genuine full archive 13 | accept | Sparkle ✔, OpenSSL ✔ |
| A2 | genuine delta 13←12 | accept | Sparkle ✔ |
| A3 | genuine signed feed | accept | Sparkle feed ✔, OpenSSL on signed prefix ✔, contract ✔ |
| A4 | build 12 (nightly) sees update | 13 offered | selection → 13 |
| B1 | 1 flipped byte in full archive | reject | Sparkle ✘, OpenSSL ✘ |
| B2 | 1 flipped byte in delta | reject | Sparkle ✘ |
| B3 | 1 byte appended to archive | reject | Sparkle ✘ |
| C1 | archive signed by foreign key | reject | Sparkle ✘, OpenSSL ✘ |
| C2 | build-12 signature replayed for 13 | reject | Sparkle ✘ |
| C3 | 64 zero bytes as signature | reject | Sparkle ✘ |
| C4 | validly signed feed carrying a foreign-key enclosure signature | reject | Sparkle archive verify ✘ |
| C5 | feed re-signed with a foreign key | reject | Sparkle feed ✘, OpenSSL ✘ |
| D1 | replay of an older, genuinely signed feed (only build 12) to build 13 | reject | selection: nothing offered; contract ✘ "regresses below the already published build" (feed signature itself still valid) |
| D2 | validly signed feed whose newest item is 11 | reject | selection: nothing offered to 12; contract ✘ "regresses" |
| D3 | two items claim build 13 | reject | contract ✘ "duplicate build version" |
| D4 | release claims 12 while 13 is newest | reject | contract ✘ "not the newest appcast item" |
| E1 | validly re-signed feed, HTTP full enclosure | reject | contract ✘ HTTPS (feed signature itself valid) |
| E2 | validly re-signed feed, HTTP delta enclosure | reject | contract ✘ HTTPS |
| E3 | HTTP enclosure edited without re-signing | reject | Sparkle feed ✘, contract ✘ length binding |
| E4 | `…/nightly/../evil/…` enclosure | reject | contract ✘ "dot path segments" |
| F1 | feed-signature trailer stripped | reject | Sparkle feed ✘, contract ✘ |
| F2 | same-length edit of signed body | reject | Sparkle feed ✘, OpenSSL ✘ |
| F3 | unsigned content after trailer | reject | contract ✘ "final content" |
| F4 | validly signed item in unknown channel `future` | reject | contract ✘ "foreign channel" |
| F5 | nightly items served as stable feed | reject | contract ✘; selection offers nothing to stable |
| F6 | nightly items to a beta build | not offered | selection offers nothing |

Result: 26/26 PASS.

### Findings

1. **Signatures alone do not stop replay or weakening.** Replaying an older,
   genuinely signed feed (D1) still passes the signature check. So does a feed
   with an `http://` enclosure that was re-signed by the key holder (E1).
   Protection comes from Sparkle's strictly-newer version check and from the
   release contract, not from the signature. The contract previously did not
   reject D1-D4, E2 or E4, so this change extends it (below).
2. **Delta enclosures were not validated before.** The contract checked only the
   full `<enclosure>`. An HTTP or off-base delta would have passed.

## Contract extensions in `tools/release/sparkle.py`

These extensions follow the product contract: Ed25519-signed appcast, delta and
full updates, safe rejection of manipulated manifests, and channel separation.
They also follow `release-policy.json` `downgradePrevention:
signed-appcast-version-policy` and `docs/UPDATES.md`.

- duplicate `sparkle:version` values are rejected;
- `expected_build` must be the newest item;
- new optional `published_build_floor`: the newest item must not fall below the
  build already published on that channel;
- `sparkle:minimumUpdateVersion`, when present, must be numeric and lower than
  the item version;
- every `sparkle:deltas/enclosure` follows the same rules as a full enclosure
  (HTTPS, credential-free, reviewed artifact base, 64-byte signature, positive
  length), and `deltaFrom` must be numeric and lower than the item version;
- enclosure paths with `.` or `..` segments, including percent-encoded ones, are
  rejected, so an enclosure cannot pass the artifact-base prefix check and then
  escape it.

The release CLI does not pass `published_build_floor` yet. Wiring it needs the
real published-feed state, which is an owner/infrastructure input.

## Proven here vs. still open

Proven locally, on tools and contract level:

- The pinned official Sparkle 2.9.6 signer and verifier accept genuine full and
  delta archives and a signed feed.
- They reject byte tampering, foreign or replayed signatures, and feed edits.
- OpenSSL agrees independently on every signature decision it was asked about.
- The repository appcast contract rejects all of the following: downgrade and
  replay feeds, duplicate versions, HTTP full and delta enclosures, base escapes,
  stripped or trailing trailers, and foreign-channel items.

Still needs a signed installed candidate or the owner (UPDATE-01…10):

- **Owner:** a real Developer ID signed, notarized and stapled Sparkle-bearing
  app. The dev build `out/AhoiDev` has Sparkle 2.9.6 embedded, but has no
  `SUFeedURL`/`SUPublicEDKey` (`AhoiSparkleFeedConfigured=false`), so its
  updater stays fail-closed by design.
- **Owner:** production Ed25519 update keys per channel, held only in release
  Keychain or infrastructure, and reviewed HTTPS feed and artifact URLs in
  `config/release-policy.json`.
- **Owner/infrastructure:** a real hosted feed and a publication transaction
  (artifacts first, appcast last, atomic restore).
- **Needs an installed candidate:** in-app journeys through the actual
  `SPUUpdater`:
  - N-2 and N-1 updates to the RC, delta and full fallback (UPDATE-01…04);
  - interrupted download, network loss and resume (UPDATE-05/06);
  - in-app rejection of a tampered feed or archive (UPDATE-07/08; this testbed
    proves the verifier and contract, not the running app's UI path);
  - channel separation on real builds (UPDATE-09);
  - profile migration (UPDATE-10);
  - relaunch.

## Files

- `tools/release/update_testbed.py`: testbed (scenarios A-F, provenance
  binding, throwaway key, Keychain guard)
- `scripts/release/ahoi-update-testbed.py`: entry point
- `tools/release/sparkle.py`: contract extensions
- `tests/repository/test_update_testbed.py`: policy unit tests, channel mirror
  vs. `update_channel.cc`, Keychain guard, and the full official-tool run
  (skipped only if the fetched tools are absent)
- `report.json`, `console.txt`: output of the run above; working paths are
  shown as `<work>`, and no private key material is included
