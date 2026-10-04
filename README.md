# AhoiBrowser

AhoiBrowser is an open-source, Apple-Silicon-only macOS browser built as a
small, reviewable overlay on Chromium's native `//chrome` product. Its focus is
an Arc-like vertical workspace tree with complete drag-and-drop, real two-,
three-, and four-pane split views, excellent developer convenience, strict
privacy defaults, first-class HTTP authentication, and full browser behavior
without an Electron, CEF, WKWebView, or web-app shell.

The complete product contract is
[`outputs/AhoiBrowser-Master-Zielprompt.md`](outputs/AhoiBrowser-Master-Zielprompt.md).
It is normative until individual requirements are captured by a more specific
architecture decision, test, or release gate.

## Current status

The current pin is Chromium Mac Stable `154.0.8037.93` at exact commit
`f89f3a4363808e117c592adedcf9947882ac3b79`. Development candidate source
`92facb5823ebd3928e1ddf27566e64a3179fd605` is installed at
`/Applications/AhoiBrowser.app` on the development and Inhouse Macs. Its guarded
build, stable development signature, portable bundle/provenance verification
and atomic installation passed. It fixes primary-frame fetches being mistaken
for navigations in developer header/cache policy. The exact installed affected
cache/header journey passed 12/12; 55 focused native factory/navigation/header-
materialization regressions passed
([evidence](artifacts/tests/document-request-mode-fixed-20261004/receipt.json)).
Original failed and focus-cancelled runs remain. Full product/release gates stay
open; this is affected feature acceptance, not complete browser acceptance.
Use [the desktop checkpoint](docs/ACTIVE_DESKTOP_CHECKPOINT.md) for the current
finding, evidence and next action; historical candidates retain their own receipts.
The active source delta is the
tracked overlay plus the complete ordered series declared in
`patches/chromium/series`; that file is the single source of truth for patch
count and order. It contains the current Chromium integration seams, deterministic
platform tests, Compose guards, native sidebar/split fixes, the null-tab
extension-menu guard, Arc 1.162 sidebar-schema compatibility, the accessible
docked/floating sidebar toggle, and the compact Zen importer seam. The product contains
the profile-backed sidebar, SQLite-backed nested tree, saved/temporary live-tab
lifecycle, drag-and-drop, command bar, shared
visual language, and bounded split-view integration. The Chromium base is Stable while
the Ahoi development product channel remains `nightly`. This milestone does not
claim the master prompt's complete binary/device matrix, `CU_E2E PASS`, or a
Developer-ID-signed, notarized Ahoi Stable release. The previous M151 evidence
remains recovery/history evidence only.

## Non-negotiable boundaries

- Chromium `//chrome`, real `Browser`, `Profile`, `BrowserContext`,
  `WebContents`, and `TabStripModel` are retained.
- Chromium's multi-process model, sandbox, site isolation, GPU process,
  network service, extensions, downloads, media, permissions, DevTools,
  password store, and session restoration stay authoritative.
- Workspaces retain global history, password and extension services. The renewed
  product goal adds local isolated website sessions per workspace, not cookie
  sync or duplicated extension installations. See
  [`docs/WORKSPACE_SESSIONS.md`](docs/WORKSPACE_SESSIONS.md); not yet implemented.
- Incognito is a true off-the-record profile. Little Arc/Quick Window is not.
- Split panes are two, three, or four normal Chromium tabs/`WebContents` inside the
  existing tab model, never a parallel WebView host. See
  [`docs/SPLIT_VIEW.md`](docs/SPLIT_VIEW.md).
- No built-in broad ad blocker. Dogfood legacy support is limited to the pinned
  **Official GitHub release** uBlock Origin Classic 1.74.0 package with
  key-derived ID `fkgkibajhfbepljeaefdnfnegdcjomkh`; arbitrary and unpacked
  Manifest V2 remain blocked, and public redistribution remains gated.
- Secrets, cookies, passwords, autofill, site data, raw/unreviewed extension storage,
  incognito state, HTTP-auth credentials, and secret headers never sync.
- Supported native browser setup, trusted extension restoration and positively
  reviewed extension-setting values are required by
  [`ADR 0010`](docs/decisions/0010-full-browser-setup-sync.md); inventory alone is
  insufficient and native consent is never bypassed. Implementation remains open.
- No product telemetry, usage pings, automatic crash uploads, or experiments.

## Developer entry points

```sh
./scripts/check-host.sh
./scripts/bootstrap-depot-tools.sh
./scripts/fetch-chromium.sh
./scripts/run-chromium-hooks.sh
./scripts/build-ahoi.sh dev
./scripts/build-upstream.sh
./scripts/restore-overlay.sh  # before changing the pin for a Stable roll
./scripts/test-repository.sh
```

The binding 25 September decision uses Xcode 27.0/27A266a with macOS SDK
27.0/26A425 and iOS SDK 27.0/24A430 for development, the upstream reference
and release. Both `pinned-reference` and `compatible-development` use these
exact inputs; Xcode 26.6 is no longer required. Per-mode provenance and
release gates remain distinct: development evidence cannot satisfy release
tests. The guarded scripts do not change global `xcode-select`. See
[BUILDING.md](docs/BUILDING.md) and [the toolchain pin](config/toolchain.json).

A standalone hook run is useful as a preflight, but build scripts deliberately
rerun Chromium hooks themselves. The local hook-state JSON is evidence only and
cannot authorize a build or suppress that run.

A complete checkout/build requires ample free disk space. See
[`docs/BUILDING.md`](docs/BUILDING.md) before fetching Chromium. An explicitly
supervised checkout may set `AHOI_ALLOW_LOW_DISK=1` below the 150 GiB checkout
recommendation, but still fails below its hard 120 GiB floor. Builds use their
own 64 GiB recommendation and 32 GiB hard floor, because an existing checkout
and incremental output do not need checkout-sized free-space headroom.

## License and contribution

AhoiBrowser-authored code is licensed under GPL-3.0-or-later. Chromium and
third-party components retain their own licenses. Contributions require a
Developer Certificate of Origin sign-off. See
[`CONTRIBUTING.md`](CONTRIBUTING.md), [`docs/LEGAL.md`](docs/LEGAL.md), and the
[`docs/TRADEMARKS.md`](docs/TRADEMARKS.md) rebranding policy.
