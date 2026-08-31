# Upstream management

AhoiBrowser tracks one explicit Chromium Stable commit. The version, commit,
source URL, branch-head, and retrieval timestamp live in
`config/chromium.json`. `depot_tools` is also pinned in
`config/depot-tools.json`. Scripts refuse placeholder or malformed pins.
The active production pin is the fully rolled, pinnable Mac ARM64 Stable
`152.0.7977.65` at
`fc4d67f1788019a27e32511137ceccbd2fafdaaa`. Its tracked overlay is composed
with the complete ordered patch series declared in `patches/chromium/series`;
the series file, rather than a duplicated prose count, is authoritative. An
Early Stable version may be recorded separately as a roll candidate but cannot
silently replace this production pin.

`scripts/verify-pin-online.sh` resolves the exact version tag from the official
Chromium Git repository. Lightweight tags must point directly to the configured
commit; annotated tags are checked through their recursively peeled `^{}`
target. The Gitiles commit metadata must also bind that object to the configured
`refs/branch-heads/*` commit position and `Cr-Branched-From` main-branch point.
The branch-point object's own main commit position, `chrome/VERSION`, and
exactly one fully rolled, pinnable active Mac ARM64 Stable VersionHistory
record are checked in the same run. Same-version records for incomplete or
non-pinnable rollout states do not make an otherwise unique eligible record
ambiguous. A matching version file alone is therefore not accepted as pin
evidence.

The online script resolves the exact tag and branch-head through Gitiles' HTTPS
ref endpoints on the same official Chromium host. This avoids relying on the
Git smart-HTTP endpoint, which may stall during large dependency operations,
without weakening the source or object-ID checks. Every request has bounded
connect, transfer, and retry timeouts and remains fail-closed.

## Checkout policy

Chromium source and build output live outside Git history under
`AHOI_WORK_ROOT`. The Ahoi repository contains only configuration, scripts,
Ahoi-owned overlay code, and documented patches. Bootstrap and sync operations
must be idempotent and must never reset or discard an unknown dirty checkout.
The checkout verifier records both the DEPS-declared and actual `gclient`
manifests. Git revisions are compared to the declared closure, expected Git
checkouts must exist and be clean, and all CIPD versions are batch-resolved to
instance IDs and compared with the installed instances. Merely recording the
current nested HEADs is not accepted as proof of the pinned dependency closure.
Source synchronization itself runs without hooks. Hooks are a separate pinned
toolchain operation. Every M152 build reruns them against the exact pinned Xcode
26.6/17F113, macOS SDK 26.5/25F70, and iOS SDK 26.5/23F81a tuple after checking
the dependency closure. Upstream/release and development keep separate
provenance labels even though they resolve to that same installation, so a
forged, stale, or development-labeled state file cannot bypass a release hook
gate. Fetch and a new hook run invalidate the previous record; only a
successful run followed by checkout revalidation atomically publishes a
diagnostic state record bound to the Chromium commit, DEPS hash, exact checkout
delta, checkout mode, Xcode version/build, and lane-specific SDK versions and
builds.

`tools/compose_overlay.py --base-revision REVISION` can compose the overlay and
ordered patches against an explicit historical commit without moving or
modifying the checkout's HEAD, index, worktree, or object database. Composition
uses temporary index and object storage, with the real object database mounted
read-only as an alternate. The flag defaults to `HEAD`; the requested ref is
resolved once to an exact commit before composition. The active M152 series has
23 entries through `0023-ahoi-sidebar-presentation-geometry.patch`; the
former 21-entry M151 series and its green matrix remain
historical recovery evidence rather than inputs to the active checkout.

For the later, deliberate checkout switch, an existing clean promisor checkout
may use `scripts/fetch-chromium.sh --prehydrate-target`. Unlike the narrow
patch-path hydration below, this opt-in inventories every unique blob in the
exact production pin and fetches only those not already present. Small
HTTP/1.1 batches, retries with adaptive splitting, atomic progress reports, and
immutable object IDs make the operation safely resumable before `gclient sync`.
It never resets or checks out a revision itself.

## Checkout-preserving roll dry-run

Discover the highest active, fully rolled and pinnable Mac ARM64 Stable release
from bounded Chromium Dash, VersionHistory and Gitiles requests without changing
the production pin:

```sh
python3 tools/chromium_roll.py discover --online --output /private/tmp/ahoi-chromium-candidate.json
```

Review that bounded discovery result and record its SHA-256, then bind exactly
the reviewed file, version, and commit as the non-production roll candidate.
The three explicit acceptance values prevent an unnoticed file replacement or
metadata edit between review and promotion. Promotion revalidates the complete
pin schema, requires a version newer than production, atomically writes only
`config/upstream-roll-candidate.json`, and never changes `config/chromium.json`:

```sh
shasum -a 256 /private/tmp/ahoi-chromium-candidate.json
python3 tools/chromium_roll.py promote-candidate \
  --candidate /private/tmp/ahoi-chromium-candidate.json \
  --accept-sha256 SHA256 \
  --accept-version VERSION \
  --accept-commit COMMIT
```

Fetch only that tag's commit and tree closure, without automatic maintenance or
an eager blob transfer:

```sh
git -C .work/chromium/src fetch --no-tags --filter=blob:none --depth=1 \
  --no-auto-maintenance https://chromium.googlesource.com/chromium/src.git \
  refs/tags/VERSION:refs/ahoi/upstream/VERSION
```

Hydrate only missing target blobs named by the ordered patch series. Explicitly
bound inputs such as `DEPS` and `chrome/VERSION` can be added repeatably; they
pass through the same path and object verification:

```sh
python3 tools/chromium_roll.py hydrate --target COMMIT \
  --include-path DEPS --include-path chrome/VERSION \
  --output /private/tmp/ahoi-chromium-hydration.json
```

Hydration uses HTTPS Gitiles responses with redirects disabled, strict response
and aggregate limits, path/request caps, a total deadline, strict base64
decoding, and an exact Git blob SHA-1 check. The target must match either the
validated production pin or `config/upstream-roll-candidate.json`; the report
cryptographically binds the ordered patch inputs. Responses are verified in
small bounded batches before those missing blobs are added to the Chromium Git
object store. Those immutable writes are deliberately resumable rather than
transactional: a rate limit or later object-write failure can leave earlier
verified batches in place, and the next run skips them safely. Transient HTTP
429 and server errors use a small per-request retry budget. HEAD, the real index
(including a stale index), and the worktree must remain byte-identical.

Report outputs are reserved before hydration, use a pinned directory descriptor
and an atomic leaf replacement, and are refused inside the Chromium checkout or
outside `artifacts/build` when they are written inside this repository. Avoid
`blob:limit` or another whole-tree refetch: it transfers unrelated source blobs
and defeats this bounded patch-path workflow.

Then classify the complete overlay and ordered patch stack without checking the
target out:

```sh
python3 tools/chromium_roll.py preflight --target COMMIT --output /private/tmp/ahoi-chromium-preflight.json
```

The efficient sequence is therefore `discover` -> explicit candidate promotion
-> filtered commit/tree fetch -> `hydrate` -> `preflight`. Preflight disables Git
optional locks before its first snapshot, uses a temporary index and object
directory, verifies that HEAD, the real index and worktree remain
byte-for-byte/status-equivalent, and reports each patch as `applies`,
`already_upstream`, or `conflict`. Lazy fetching is disabled;
`write-tree --missing-ok` preserves untouched promisor references without requesting
unrelated blobs. A report containing either of the latter dispositions exits
with status 2 and is not roll-ready. Offline discovery accepts the seven explicit
captured response files shown by `discover --help`; neither production
`config/chromium.json` nor the reviewed candidate binding is an unchecked report
output target.

## Roll policy

1. Read the Chromium Stable release announcement, run discovery, review its
   exact version/commit, promote the candidate binding, hydrate, and preflight.
2. While `config/chromium.json` still names the old pin, restore an applied Ahoi
   checkout through `./scripts/restore-overlay.sh`. The command accepts only the
   exact current pin, current overlay inputs, recorded state, and complete
   checkout tree; it performs no reset or broad cleanup.
3. Update `config/chromium.json` to the reviewed candidate and remove the
   candidate binding in the same reviewed commit.
4. Run `./scripts/fetch-chromium.sh --prehydrate-target`, sync dependencies, and
   verify no unexpected solution/repository changes.
5. Reapply every patch independently; record conflicts and disposition.
6. Build unmodified Chromium, then AhoiBrowser.
7. Run repository, unit, integration, security, installed-app CU E2E, update,
   crash-recovery, and performance gates.
8. Update third-party license notices and source-offer artifacts.

Critical Chromium security updates target a releasable Ahoi build within 48
hours. Routine Stable rolls target seven days. A missed target blocks unrelated
feature releases until the security delta is resolved.

## Branding and services

Chromium trademarks, Google API keys, Chrome Sync, Chrome Web Store update
services, Widevine, proprietary codecs, Safe Browsing transport, and other
Google-controlled services are separate legal/technical integrations. A
working Chromium build does not grant rights or credentials for them. Each is
feature-gated and documented before release.
