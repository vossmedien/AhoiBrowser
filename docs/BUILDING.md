# Building

## Supported host

Phase 0 targets Apple Silicon with macOS 26, exact Xcode 26.6 (17F113), macOS
SDK 26.5 (25F70), iOS SDK 26.5 (23F81a), Git, APFS, and at least 150 GiB of
free working space. Chromium M152 pins that same Xcode and SDK tuple for the
upstream control, Ahoi development, and release paths. The paths retain
different `pinned-reference` and `compatible-development` provenance labels;
the latter does not turn development evidence into release evidence.
Repository/build tooling requires Python 3.9 or newer.
The authoritative upstream requirements are recorded alongside the Chromium
pin; if Chromium requires a different Xcode/SDK, the host check fails clearly.

A full checkout and optimized build can exceed 100 GiB. Do not place it on a
nearly full system volume. Set an explicit absolute work root when needed:

```sh
export AHOI_WORK_ROOT="/absolute/path/to/ahoi-work"
export AHOI_XCODE_DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer"
```

The canonical work root may contain spaces. M152's pinned V8 revision still
emits absolute Inspector-Protocol template paths into one depfile, and
Chromium's Rust wrapper cannot parse rustc's escaped absolute `OUT_DIR` paths.
Both build scripts therefore use the exact temporary workarounds pinned in
`config/dependency-build-workarounds.json`. The wrapper applies them only
while `gn gen` and `autoninja` run, restores both target files byte-for-byte
with their original modification times on success, failure, or signal, and
writes every verified target and patch SHA-256 into build provenance.
Preserving the original times keeps Ninja's incremental frontier intact, so a
resumed build does not rebuild thousands of already valid outputs merely
because the temporary workaround was restored. A milestone update fails closed
until the new Chromium and V8 revisions, original target bytes, and patch
applicability are reviewed and repinned.

For an explicitly supervised checkout or build, `AHOI_ALLOW_LOW_DISK=1`
permits starting below the recommended 150 GiB but never below the configured
120 GiB absolute floor. The override emits a warning and changes neither the
recommendation nor the evidence required from the resulting build; it is not a
release-pass signal by itself.

## Bootstrap

```sh
./scripts/check-host.sh
./scripts/bootstrap-depot-tools.sh
./scripts/fetch-chromium.sh
./scripts/run-chromium-hooks.sh
```

During a Stable milestone roll, an existing clean partial-clone checkout can
prehydrate the exact pinned target before `gclient` changes its `HEAD`:

```sh
./scripts/fetch-chromium.sh --prehydrate-target
```

This opt-in mode is not used for the first checkout. It obtains missing
commit/tree metadata without writing a ref when necessary, then inventories
the pinned target with lazy fetching disabled and downloads only missing unique
blobs in small HTTP/1.1 batches. The immutable object-store progress is
resumable; rerunning the same command skips blobs already present. The report
is atomically written to
`artifacts/build/chromium-checkout-hydration.json`. Worktree, index, `HEAD`,
refs, `FETCH_HEAD`, and shallow-boundary changes fail closed before the normal
sync begins.

The depot_tools bootstrap disables automatic repository updates before running
the pinned checkout's `ensure_bootstrap` through an absolute
`DEPOT_TOOLS_DIR`. It then rejects a missing, absolute, escaping, or
non-executable `python3_bin_reldir.txt` target and re-verifies the configured
origin, exact commit, and clean checkout. A successful `gclient --help` alone
is not bootstrap evidence because that command can bypass depot_tools' Python
initialization.

The fetch script never silently deletes a checkout, resets local edits, or
changes a checkout whose origin is not official Chromium. It records resolved
Git and CIPD dependency closures under `artifacts/build/`, comparing the
DEPS-declared revisions with the actual installed revisions. Fetch deliberately
uses `--nohooks` and creates/revalidates the byte-exact, reviewable
`config/gclient.py`; local solutions, `custom_vars`, or target overrides are
rejected before sync and by all later provenance gates. The default hook step
fails closed unless exact Xcode 26.6, its SDK builds, dependency closure, clean
checkout, and build disk headroom all match. For local iteration,
`--compatible-dev-xcode` selects the separately labeled development entry,
which currently resolves to the same Xcode 26.6/17F113 and SDK builds. That
state remains rejected by the upstream/release provenance path despite the
byte-identical toolchain. Fetch invalidates the prior hook record
before every sync. More importantly, both build scripts run `gclient runhooks`
again themselves before `gn gen`; they never use the freely writable state JSON
as authority to skip execution. The Ahoi build uses the same gate while the
verified source overlay is present. State and artifact JSON files are removed
before execution and atomically published only after runhooks and the post-run
checkout verification succeed. They are diagnostic evidence, not a trust root.

## Baseline build

```sh
./scripts/build-upstream.sh
```

This produces an unbranded, unmodified Chromium ARM64 baseline first. It is the
control used to distinguish upstream/toolchain failures from Ahoi patches.
Production flags are intentionally separate from fast developer flags. Its
machine-readable provenance binds the app and binary hashes, generated and
configured GN arguments, complete dependency closure, Chromium/depot_tools
commits, trusted GN/Ninja/Clang/LLD binary hashes, and exact Xcode/SDK versions.

## Ahoi build

```sh
./scripts/apply-overlay.sh
./scripts/build-ahoi.sh
```

With the pinned Xcode 26.6 toolchain, bootstrap and apply the overlay explicitly
as follows; `build-ahoi.sh dev` then selects the same installation under the
development provenance label automatically:

```sh
./scripts/run-chromium-hooks.sh --compatible-dev-xcode
./scripts/apply-overlay.sh --compatible-dev-xcode
./scripts/build-ahoi.sh dev
```

The overlay script is idempotent and transactional. Its initial application
composes the standalone overlay plus the ordered patch series in an isolated Git
index, applies one validated delta, verifies the complete resulting tree, and
atomically publishes a new pin-bound state file. A state-publication failure or
interrupt rolls back only that exact delta; there is no reset fallback. Running
the same command after changing overlay files or the patch series safely
refreshes an already applied checkout: the recorded
`checkoutDeltaFingerprint` must first match the complete current checkout tree,
then only the checked old-tree-to-new-tree delta is applied and the state file
is replaced atomically. There is no reset or broad cleanup fallback; unrelated,
partial, staged, or foreign dirty state is refused and left untouched. A source
tree change invalidates prior hook evidence so the next build reruns hooks;
input-only changes that produce the identical tree update only overlay state.
Packaging, signing, notarization, stapling, DMG generation, and `/Applications`
installation are separate gates; a successful `autoninja` invocation is not a
release.

Before changing `config/chromium.json` for a Stable roll, return an applied
checkout to the still-current pinned Chromium tree with:

```sh
./scripts/restore-overlay.sh
```

Restore recomposes the current source inputs, verifies the state and complete
checkout tree, reverse-applies only their exact delta, removes that state, and
invalidates hook evidence. Any mismatch is refused without broad cleanup. Run it
before editing the production pin; after the pin changes, the old state is
deliberately no longer authoritative.

The dependency-workaround build wrapper accepts one or more explicit Ninja
targets. Focused unit suites can therefore reuse the same pinned path-space
workarounds as `chrome` without a second ad-hoc build path, and several related
targets share one `gn gen` plus one temporary patch/apply/restore cycle.
`build-ahoi.sh` always includes `chrome` and accepts additional targets, for
example. Before deleting an old workaround receipt or invoking any build tool,
the wrapper canonicalizes the output directory, requires an absolute child of
the Chromium `out` directory, and rejects dot components, control characters,
non-directory components, and every existing symlink component:

```sh
./scripts/build-ahoi.sh dev ahoi_developer_toolkit_unittests \
  ahoi_developer_toolkit_ui_unittests
```

## Stable development signing and Keychain access

`build-ahoi.sh dev` signs the finished bundle with the single valid
`Apple Development` identity available in the current macOS Keychain. This is
local development signing only: the `Developer ID Application` identity remains
reserved for the notarized release pipeline. If multiple development identities
exist, select the intended one by its exact displayed name:

```sh
export AHOI_DEV_CODESIGN_IDENTITY='Apple Development: Name (TEAMID1234)'
```

The stable identity is important for ordinary browser profiles. Chromium keeps
the profile encryption secret in its macOS Safe Storage Keychain item, whose
access control evaluates the requesting app's designated code requirement. An
ad-hoc signature has only a content-dependent CDHash requirement; every changed
build therefore looks like a new requester and can trigger another macOS
password prompt. An Apple Development signature has a stable Team ID and
certificate-backed requirement across normal incremental builds.

This does not promote the component-based development bundle into a release
candidate. Its staged component libraries retain their linker signatures and
the development signer does not enable a partial hardened runtime around them.
The non-component release pipeline signs every nested code object with the
reviewed entitlements before enabling hardened runtime, notarization and
distribution gates.

The first launch after moving from the old ad-hoc signature can still prompt
once because the existing Keychain ACL knows the preceding CDHash. Authorize
that expected AhoiBrowser request with **Always Allow** to update the access
decision; the build tooling deliberately does not delete the Safe Storage item,
rewrite its ACL, expose its secret, or weaken Keychain security.

Identity discovery fails closed if no valid Apple Development identity exists.
Configure one through Xcode before normal profile work. Disposable environments
which intentionally accept repeated Keychain prompts may opt into ad-hoc signing
explicitly:

```sh
export AHOI_ALLOW_ADHOC_DEV_SIGNING=1
export AHOI_DEV_CODESIGN_IDENTITY=-
```

That fallback is not suitable for daily-driver profiles or release evidence.
End users do not need a local signing identity; distributed builds use the
project's Developer ID, notarization, and update chain.

## Google API keys

AhoiBrowser intentionally builds and runs without Google API keys or OAuth
secrets. Ordinary browsing, downloads, media, permissions, DevTools and the
Chromium extension runtime do not require a user-supplied key. The product UI
must therefore never ask an end user to configure one or show Chromium's
developer-only missing-key infobar.

Google restricts private Chrome services such as Chrome Account Sync to
authorized Google products. Ahoi does not embed shared credentials or pretend
that adding a personal key enables Chrome Sync; cross-device browser state uses
the independently documented Ahoi Sync provider instead. Any optional future
Google-backed integration requires its own explicit product, privacy, quota and
distribution review and remains separate from core browsing.

## Build profiles

- `upstream-release`: unmodified Chromium control, ARM64, non-component.
- `ahoi-dev`: faster local Ahoi iteration while retaining sandbox behavior;
  uses the development provenance label for the pinned Xcode 26.6 toolchain.
- `ahoi-release`: optimized, non-component, unsigned candidate for the later
  signing and notarization pipeline.

All profiles keep Chromium M152's `mac_deployment_target = "13.0"` while
setting `mac_min_system_version = "26.0"`. The first value controls SDK symbol
availability and deprecation diagnostics throughout the upstream Chromium
core; the second writes the product's actual `LSMinimumSystemVersion` and makes
AhoiBrowser macOS-26-only. Chromium documents these as deliberately separate
levers so an application may raise its launch requirement before the entire
upstream source tree is migrated away from every older SDK API. Ahoi-owned
macOS-26 AppKit APIs belong in narrow Objective-C++ adapters guarded with
`@available(macOS 26.0, *)` (or `__builtin_available`), with no AppKit-26 types
leaking into generic Chromium headers. Do not use the deployment-target
preprocessor macro to remove the new code: the shared compiler target remains
13 by design while the linked SDK remains 26.5.

No supported profile uses `--no-sandbox`, `--ignore-certificate-errors`, or a
disabled site-isolation mode.
Selecting Xcode 26.6 through the development label is deliberately not treated
as upstream/release provenance: identical toolchain bytes do not let a
development build satisfy control, signed-candidate, or release gates.
