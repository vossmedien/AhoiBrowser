# AhoiBrowser

AhoiBrowser is an open-source, Apple-Silicon-only macOS browser built as a
small, reviewable overlay on Chromium's native `//chrome` product. Its focus is
an Arc-like vertical workspace tree with complete drag-and-drop, real two- and
three-pane split views, excellent developer convenience, strict privacy
defaults, first-class HTTP authentication, and full browser behavior without an
Electron, CEF, WKWebView, or web-app shell.

The complete product contract is
[`outputs/AhoiBrowser-Master-Zielprompt.md`](outputs/AhoiBrowser-Master-Zielprompt.md).
It is normative until individual requirements are captured by a more specific
architecture decision, test, or release gate.

## Current status

Phase 0 is in progress. In addition to the upstream pin, reproducible
patch/overlay workflow and evidence contracts, the current twelve-patch stack
now contains a native profile-backed sidebar, a durable SQLite-backed nested
tree with non-blocking UI access, live Chromium tab bridge, local command bar,
shared visual language and bounded split-view integration. The
development app builds and launches, but it is not yet a daily-use browser and
no compatibility claim is considered proven without installed-app evidence.

## Non-negotiable boundaries

- Chromium `//chrome`, real `Browser`, `Profile`, `BrowserContext`,
  `WebContents`, and `TabStripModel` are retained.
- Chromium's multi-process model, sandbox, site isolation, GPU process,
  network service, extensions, downloads, media, permissions, DevTools,
  password store, and session restoration stay authoritative.
- Workspaces are UI/session organization inside one normal profile, never
  separate cookie or extension profiles.
- Incognito is a true off-the-record profile. Little Arc/Quick Window is not.
- Split panes are two or three normal Chromium tabs/`WebContents` inside the
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
./scripts/test-repository.sh
```

The upstream/release lane is pinned to Xcode 26.5/17F42. Local `ahoi-dev`
iteration may use the separately pinned Xcode 26.6/17F113 compatibility lane;
their iOS SDK builds are bound separately as 23F73 and 23F81a. The development
lane cannot satisfy release provenance or release tests.

A standalone hook run is useful as a preflight, but build scripts deliberately
rerun Chromium hooks themselves. The local hook-state JSON is evidence only and
cannot authorize a build or suppress that run.

A complete checkout/build requires ample free disk space. See
[`docs/BUILDING.md`](docs/BUILDING.md) before fetching Chromium.

## License and contribution

AhoiBrowser-authored code is licensed under GPL-3.0-or-later. Chromium and
third-party components retain their own licenses. Contributions require a
Developer Certificate of Origin sign-off. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) and [`docs/LEGAL.md`](docs/LEGAL.md).
