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

Phase 0 is in progress on Chromium Mac Stable `152.0.7977.65` at exact commit
`fc4d67f1788019a27e32511137ceccbd2fafdaaa`. The active source delta is the
tracked overlay plus the three-entry series in `patches/chromium/series`: the
M152 integration seams, deterministic platform tests, and upstream page-load
tracing isolation. It contains the profile-backed sidebar, SQLite-backed nested
tree, saved/temporary live-tab lifecycle, drag-and-drop, command bar, shared
visual language, and bounded split-view integration. The current M152 ARM64
development build is installed at `/Applications/AhoiBrowser.app`; focused
tests and a visible installed-app compatibility smoke, including repeated
sidebar collapse/restore and resize, passed. The Chromium base is Stable while
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
- Workspaces are UI/session organization inside one normal profile, never
  separate cookie or extension profiles.
- Incognito is a true off-the-record profile. Little Arc/Quick Window is not.
- Split panes are two, three, or four normal Chromium tabs/`WebContents` inside the
  existing tab model, never a parallel WebView host. See
  [`docs/SPLIT_VIEW.md`](docs/SPLIT_VIEW.md).
- No built-in broad ad blocker. Selective legacy support exists only for an
  explicitly allowlisted uBlock Origin Classic package when legally and
  technically viable.
- Secrets, cookies, passwords, autofill, site data, extension storage,
  incognito state, HTTP-auth credentials, and secret headers never sync.
- No product telemetry, usage pings, automatic crash uploads, or experiments.

## Developer entry points

```sh
./scripts/check-host.sh
./scripts/bootstrap-depot-tools.sh
./scripts/fetch-chromium.sh
./scripts/run-chromium-hooks.sh
./scripts/build-upstream.sh
./scripts/restore-overlay.sh  # before changing the pin for a Stable roll
./scripts/test-repository.sh
```

All M152 build profiles use the same pinned Xcode 26.6/17F113 installation,
macOS SDK 26.5/25F70, and iOS SDK build 23F81a. The `pinned-reference`
upstream/release mode and `compatible-development` development mode remain
separate provenance labels and gates even though their toolchain bytes match;
development evidence still cannot satisfy release tests.

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
