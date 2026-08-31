# AhoiBrowser local HTTPS E2E fixture

This dependency-free fixture supplies deterministic, loopback-only browser
journeys for the general E2E matrix. It is test infrastructure, not production
browser code. Passing its self-tests proves the fixture contract only; it does
not prove an installed AhoiBrowser journey.

The cluster exposes three distinct HTTPS origins on `127.0.0.1`, addressed as
`first-party.localhost`, `third-party.localhost`, and `media.localhost`. A
per-run P-256 test CA signs one localhost-only leaf certificate. Browser runs
must keep normal TLS validation enabled: `--ignore-certificate-errors`, an
insecure HTTP replacement, or a globally permissive TLS policy are forbidden.

## Explicit certificate and trust workflow

No command silently changes a trust store. Certificate generation writes only
to the selected runtime directory. Trust installation requires an exact,
deliberately supplied confirmation phrase and targets one macOS **user**
keychain. The CA private key and leaf private key are never imported.

From the repository root:

```sh
python3 tools/ahoi_e2e_fixture.py generate-certificates
python3 tools/ahoi_e2e_fixture.py trust-install \
  --confirm I-understand-this-adds-a-local-test-CA
python3 tools/ahoi_e2e_fixture.py start
python3 tools/ahoi_e2e_fixture.py status
```

The default runtime directory is `/tmp/ahoibrowser-e2e-$UID`. Every command
accepting state also accepts `--state-dir PATH`. `trust-install` uses the
default macOS user keychain reported by `security default-keychain -d user`, or
an explicitly supplied `--keychain PATH`. It writes `trust-receipt.json` with
the exact keychain and CA fingerprints only after `security add-trusted-cert`
succeeds. `start` refuses to run without that explicit receipt, an exact
fingerprint match from `security find-certificate`, and a local-only
`security verify-cert` check of the fixture leaf against the recorded keychain.

After an installed-browser run, always clean up in this order:

```sh
python3 tools/ahoi_e2e_fixture.py stop
python3 tools/ahoi_e2e_fixture.py protocol-remove \
  --confirm remove-the-isolated-ahoi-e2e-protocol-handler
python3 tools/ahoi_e2e_fixture.py trust-remove \
  --confirm remove-the-local-test-CA
python3 tools/ahoi_e2e_fixture.py cleanup
```

`trust-remove` calls `security delete-certificate -t` for exactly the SHA-256
fingerprint and user keychain captured by `trust-install`; it does not search
by a broad name and does not touch any other certificate. `cleanup` refuses
while a trust receipt remains, then deletes the local CA/leaf certificates and
their private keys. It retains `receipts.jsonl` and `service.log` for audit.
Delete those diagnostic files separately only after preserving required test
evidence.

`stop` deliberately does not alter trust. This makes a crash or interrupted
test observable instead of hiding a trust mutation. A retained
`trust-receipt.json` is an explicit cleanup obligation.

## Origins and default ports

| Role | Default URL | Purpose |
|---|---|---|
| first party | `https://first-party.localhost:28443` | journeys, uploads/downloads, storage, receipts |
| third party | `https://third-party.localhost:28444` | CHIPS, cross-origin redirects and CORS |
| media | `https://media.localhost:28445` | deterministic H.264/AAC media and MSE/PiP |

Use `--first-port`, `--third-port`, or `--media-port` with `start` when the
defaults are occupied. All sockets bind only to `127.0.0.1`; the leaf SAN also
contains `localhost`, the three role names, `127.0.0.1`, and `::1` so the same
CA can be used by a strict test client.

The first-party root links every visual journey. The machine-readable contract
is available at `/__fixture/manifest`; `/__fixture/health` provides readiness,
`/__fixture/receipts` returns sanitized receipts, and `POST
/__fixture/reset` clears fixture receipts and counters, including re-arming the
single intentional download disconnect.

## Isolated custom-protocol workflow

The navigation page includes exactly one non-web URL:
`ahoi-e2e-safe://open/fixture`. It remains inert unless the optional fixture
handler is installed with explicit consent:

```sh
python3 tools/ahoi_e2e_fixture.py protocol-install \
  --confirm install-the-isolated-ahoi-e2e-protocol-handler
python3 tools/ahoi_e2e_fixture.py protocol-status
```

The installer creates an ad-hoc-signed AppleScript application only at
`$STATE_DIR/AhoiBrowser E2E Protocol Handler.app`. Its fixed event recorder is a
self-contained, owner-readable Python helper inside that signed bundle; it does
not import or execute the repository copy of `custom_protocol.py`. The handler
compares the one accepted URL case-sensitively, then verifies the exact bundle
with `codesign --verify --deep --strict` and the embedded helper with its pinned
SHA-256 before invoking it without arguments. Arbitrary hosts, paths, queries,
fragments, case variants and other schemes are ignored. The incoming value is
never passed to a shell or retained; an accepted invocation appends only a
fixed, owner-only event to `protocol-handler-events.jsonl` through no-follow
directory/file descriptors.

An existing handler is reused only when its owner-only receipt, exact marker,
installation identifier, compiled handler hash, bundled-helper hash,
`Info.plist` hash, marker hash and strict code-signature/identifier verification
all agree. Replacement and removal authority requires the receipt and the
in-bundle marker to match each other byte-for-byte through the recorded marker
hash; a stale receipt alone never authorizes deletion. A damaged or legacy app
whose marker/receipt contract no longer matches must be inspected and manually
quarantined rather than automatically replaced or removed.

Before any registration mutation, the installer reads the LaunchServices
database and requires that every claim for the fixture bundle ID or
`ahoi-e2e-safe` scheme is either absent or the exact current state-directory app
path. A foreign handler, a handler from another state directory, a pathless
claim or an unreadable registry fails closed without exposing the foreign path
in status output. Registration and unregistration are polled until the exact
path set is observed, and transactional rollback preserves the prior
registration state and restores the app and receipt if staging, registration,
integrity verification or receipt commit fails; removal restores a prior
registration when app-tree deletion fails.

Removal still requires the exact phrase shown in the cleanup workflow.
`cleanup` refuses while any handler app, receipt or transaction-backup artifact
remains, including a damaged fixture that reports `needsRepair`. If rollback or
backup cleanup itself cannot complete, the tool stops with a manual-cleanup
requirement instead of deleting an ownership-ambiguous artifact. This unique
test scheme has no production handler to replace, and the fixture never
registers AhoiBrowser itself.

## Journey and endpoint contract

| Area | Pages/endpoints | What the fixture proves |
|---|---|---|
| download | `/download-upload`, `/download/deterministic.bin` | `Accept-Ranges`, single byte ranges, pause/resume reconstruction, stable SHA-256 |
| large/resume download | `/download/large-range.zip` | deterministic 12 MiB stored ZIP, throttled 64 KiB chunks, Range and stable SHA-256 |
| disconnect/resume | `/download/disconnect-once.zip` | first full response after reset is deliberately truncated; subsequent Range/full requests serve the same stable ZIP |
| PDF | `/document/synthetic.pdf` | generated timestamp-free one-page PDF for built-in viewer, print and download journeys |
| warning download | `/download/harmless-warning.exe` | attachment with warning-prone extension/MIME; plain text only, no executable code or malware |
| upload/DnD | `/download-upload`, `POST /upload` | picker and drop-zone raw upload, size/hash receipt, bytes never stored |
| split/DnD | `/split`, `/pane/a`, `/pane/b`, `/pane/c` | three independent panes plus a web-URL-only drop target |
| navigation | `/navigation`, `/redirect/same`, `/redirect/cross`, `/popup` | same-/cross-origin redirects and requested/`noopener` popups |
| custom protocol | `ahoi-e2e-safe://open/fixture` | exact-match dispatch to the separately consented isolated handler; no arbitrary URL forwarding |
| OAuth | `/oauth/authorize`, `POST /oauth/approve`, `/oauth/callback` | explicitly synthetic consent/callback plumbing with no real IdP or token |
| passkey | `/passkey`, `/passkey/challenge`, `POST /passkey/verify` | local UI/challenge simulation only; no `navigator.credentials`; real platform WebAuthn remains `ASSISTED_E2E` |
| media | `/media`, `/media/sample.mp4` | committed, hash-pinned fragmented H.264 Baseline/AAC-LC MP4, Range, MSE and PiP controls |
| WebRTC/capture | `/webrtc` | local peer connection with no ICE servers, camera/mic and screen-capture prompts |
| permissions | `/permissions` | location, notifications, clipboard read/write prompts |
| privacy | `/privacy`, `/cookies/set`, `/cookies/third-party`, `/privacy/echo` | first-/third-party cookies, Secure/HttpOnly/SameSite, CHIPS, GPC, referrer and tracking-key shape |
| storage | `/storage`, `/assets/v1/data.json`, `/counter/storage`, `/service-worker.js` | local/session storage, IndexedDB, Cache Storage, SW, immutable versioned asset and counters |
| developer | `/developer`, `/headers/echo`, `/csp/strict`, `/cors/allow`, `/cors/deny` | allowlisted header echo, secret-presence redaction, strict CSP and positive/negative CORS |
| injection | `/injection` | CSS and JavaScript controls plus LESS/SASS source inputs for Ahoi's compiler |
| login | `/login` | artificial login form using only `fixture-user` / `fixture-password` |

The media fixture demonstrates a deterministic codec input, not legal approval
or licensed-service support. The WebRTC control never replaces a real Meet or
equivalent journey. The passkey control never replaces Touch ID or a true
platform WebAuthn ceremony.

## Media asset provenance and rights boundary

The committed source form is `assets/h264-aac.mp4.b64`; its decoded MP4 is
6,907 bytes with SHA-256
`c195edb6dee6e3465fb5fd5fa0a0b7f3fbbd8ac48d7c953ae7108cff777f5436`.
Container metadata identifies FFmpeg/Lavf `62.12.102`, H.264 Constrained
Baseline video encoded by `Lavc62.28.102 libx264`, and mono AAC-LC audio. No
original generation command, named author, upstream URL, copyright assignment,
or license grant was captured with the asset. The repository therefore makes
no ownership or redistribution-rights claim for these bytes. Public
redistribution remains blocked by the `proprietary-codecs` legal gate unless
review clears this exact asset or it is replaced with an approved fixture.

The following command is a documented way to create a new, synthetic equivalent
for review; it is not asserted to reconstruct the committed bytes, because
FFmpeg and encoder versions can change output. A replacement requires recording
the tool versions and command, reviewing its copyright/license and codec-patent
status, updating `MEDIA_SHA256` in `server.py`, and rerunning the fixture tests.

```sh
ffmpeg -hide_banner -y \
  -f lavfi -i 'testsrc2=size=160x90:rate=10:duration=1' \
  -f lavfi -i 'sine=frequency=440:sample_rate=44100:duration=1' \
  -map 0:v:0 -map 1:a:0 \
  -c:v libx264 -profile:v baseline -level:v 1.2 -pix_fmt yuv420p \
  -g 10 -keyint_min 10 -sc_threshold 0 -bf 0 \
  -c:a aac -profile:a aac_low -ac 1 -ar 44100 \
  -movflags +frag_keyframe+empty_moov+default_base_moof -shortest \
  /private/tmp/ahoi-h264-aac.mp4
base64 -i /private/tmp/ahoi-h264-aac.mp4 -b 76 \
  -o fixtures/e2e/assets/h264-aac.mp4.b64
```

## Privacy-safe receipts

Every request appends one JSON object to `receipts.jsonl`. A receipt contains a
per-process run ID, monotonic ID, timestamp, role, method, path without query,
status, query **key names**, tracking-key classification, presence booleans for
Authorization, Cookie, Origin, Referer and GPC, plus endpoint-specific public
facts such as payload size/hash or whether a synthetic login matched.

Only known fixture route names are retained; dynamic pane identifiers are
normalized and unknown paths become `/__unmatched__`. Upload receipt facts keep
the content hash, byte count and extension, but not the original filename.

Receipts never retain:

- query values;
- Authorization, Cookie, or password values;
- uploaded bytes;
- OAuth codes/state values;
- clipboard, camera, microphone, screen, or location data;
- user-agent or client IP data.

`/headers/echo` returns only an allowlisted public `X-Ahoi-Test` value and
presence booleans for sensitive headers. `/privacy/echo` removes query data
from the reflected referrer.

## Self-tests

Run the stdlib-based suite with:

```sh
python3 tools/ahoi_e2e_fixture.py run-tests
```

The suite creates its CA and servers inside `TemporaryDirectory`, validates TLS
against only that CA, verifies that a default trust store rejects the leaf,
and mocks both `security add-trusted-cert` and `security delete-certificate`.
It never invokes either trust-changing command and leaves no system trust or
server process behind.
