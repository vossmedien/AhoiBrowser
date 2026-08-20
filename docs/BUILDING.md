# Building

## Supported host

Phase 0 targets Apple Silicon with macOS 26, exact Xcode 26.5 (17F42) for the
upstream control and release path, macOS SDK 26.5 (25F70), iOS SDK 26.5
(23F73), Git, APFS, and at least 150 GiB of free working space. Xcode 26.6
(17F113), with the same macOS SDK build but iOS SDK 26.5 (23F81a), is accepted
only for the `ahoi-dev` compatibility path. Repository/build tooling requires
Python 3.9 or newer.
The authoritative upstream requirements are recorded alongside the Chromium
pin; if Chromium requires a different Xcode/SDK, the host check fails clearly.

A full checkout and optimized build can exceed 100 GiB. Do not place it on a
nearly full system volume. Set an explicit absolute work root when needed:

```sh
export AHOI_WORK_ROOT="/absolute/path/to/ahoi-work"
export AHOI_XCODE_DEVELOPER_DIR="/Applications/Xcode-26.5.0.app/Contents/Developer"
```

For an explicitly supervised shallow checkout, `AHOI_ALLOW_LOW_DISK=1` permits
starting below the recommended 150 GiB but never below the configured 120 GiB
absolute floor. This is not accepted for a release build and does not change the
host recommendation.

## Bootstrap

```sh
./scripts/check-host.sh
./scripts/bootstrap-depot-tools.sh
./scripts/fetch-chromium.sh
./scripts/run-chromium-hooks.sh
```

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
fails closed unless exact Xcode 26.5, its SDK builds, dependency closure, clean
checkout, and build disk headroom all match. For local iteration,
`--compatible-dev-xcode` selects the explicitly pinned 26.6 compatibility
entry and its separate iOS SDK build; that state is labeled separately and
rejected by the upstream/release path. Fetch invalidates the prior hook record
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

With the already installed Xcode 26.6 development toolchain, bootstrap and
apply the overlay explicitly as follows; `build-ahoi.sh dev` then selects the
same compatible toolchain automatically:

```sh
./scripts/run-chromium-hooks.sh --compatible-dev-xcode
./scripts/apply-overlay.sh --compatible-dev-xcode
./scripts/build-ahoi.sh dev
```

The overlay script is idempotent and transactional. It composes the standalone
overlay plus the ordered patch series in an isolated Git index before applying
one validated delta, so dependent patches work and a failed composition leaves
the checkout pristine. It refuses unrelated dirty Chromium files. Packaging,
signing, notarization, stapling, DMG generation, and `/Applications`
installation are separate gates; a successful `autoninja` invocation is not a
release.

## Build profiles

- `upstream-release`: unmodified Chromium control, ARM64, non-component.
- `ahoi-dev`: faster local Ahoi iteration while retaining sandbox behavior;
  pinned to the verified Xcode 26.6 compatibility entry.
- `ahoi-release`: optimized, non-component, unsigned candidate for the later
  signing and notarization pipeline.

All profiles keep Chromium M151's `mac_deployment_target = "13.0"` while
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
Xcode 26.6 is deliberately not treated as equivalent to the M151 production
baseline: it can produce development evidence, but never the upstream control,
release provenance, signed-candidate evidence, or a release PASS.
