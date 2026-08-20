# Product principles

AhoiBrowser is a fast, native Chromium browser for people who want Arc's best
organization and convenience ideas without its accumulated product surface.
The principles below are release constraints, not aspirations.

## Browser first

AhoiBrowser must behave like a complete browser: navigation, downloads,
uploads, print/PDF, fullscreen, media, Picture in Picture, permissions,
camera/microphone/screen capture, password managers, passkeys, OAuth/SSO,
DevTools, extensions, crash recovery, and default-browser handling are core.
A custom UI is never justification for bypassing Chromium's mature services.

## Small native delta

The product is a Chromium `//chrome` fork with Chromium Views and narrow
Objective-C++/AppKit bridges. Electron, CEF, WKWebView, `content_shell`, a Node
runtime, and a browser-shaped web app are forbidden product architectures.
Every upstream modification must explain why an extension point or Ahoi-owned
target was insufficient and how the patch is tested across a Chromium roll.

## Organization without profile fragmentation

A single normal Chromium profile owns cookies, passwords, extensions, history,
permissions, and site storage. Workspaces own only visible tree/session state
and appearance. Users can move between workspaces without signing in again.
True incognito uses a Chromium off-the-record profile. Quick Window/Little Arc
uses the normal profile and must be communicated distinctly.

## Local-first privacy

No usage telemetry, engagement tracking, automatic crash upload, or remote
experiment framework is enabled. First-party login compatibility is preserved.
Cloud sync is an optional encrypted replication layer for explicitly permitted
records, never a reason to upload secrets or browsing storage.

## Developer power without idle cost

Frequently used developer actions may be first-class UI, but activate only on
demand and reuse Chromium services. Advanced behavior that changes requests,
CSP, CORS, or page execution is visible, scoped, reversible, and produces an
activity indicator. Accessibility checking remains an external extension unless
a later decision demonstrates a smaller and maintainable integration.

## Evidence over claims

A feature is complete only after automated checks and visible end-to-end proof
against the signed app installed at `/Applications/AhoiBrowser.app`. `NOT_RUN`
and every blocked status are explicit non-success states.
