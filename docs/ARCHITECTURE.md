# Architecture

## System shape

```text
AhoiBrowser Views/AppKit chrome
  -> WorkspaceTreeController (Ahoi-owned UI/session model)
  -> Chromium Browser + TabStripModel
  -> Profile / OffTheRecordProfile
  -> WebContents
  -> Chromium services
       extensions | downloads | history | passwords | permissions
       media | DevTools | network service | session restore | sync adapter

AhoiBrowser iOS/iPadOS companion (SwiftUI, no embedded browser)
  -> local encrypted store
  -> CloudKit private database adapter
  -> signed, scoped remote commands
```

## Chromium ownership boundary

Chromium remains authoritative for processes, sandbox policy, site isolation,
navigation, storage partitions, networking, TLS, authentication challenges,
content settings, extensions, media, accessibility trees, and DevTools.
Ahoi owns browser chrome presentation, workspace/tree persistence, command
surfaces, developer convenience orchestration, theme tokens, localization, and
the CloudKit record adapter.

No Ahoi service may create an alternate cookie jar, password database, download
stack, permission store, media stack, renderer, or authentication protocol.

## Desktop layers

1. **Ahoi Views UI**: sidebar/tree, toolbar, command bar, settings, permission
   indicators, download surfaces, extension controls, and developer tools.
2. **Browser integration**: maps tree leaves to `WebContents` and normal
   `TabStripModel` lifecycle without replacing Chromium navigation semantics.
3. **Persistence**: local database for workspace/tree records using stable UUIDs,
   hybrid logical clocks, sortable order keys, and tombstones.
4. **Cloud adapter**: local-first change journal mapped to CloudKit private-zone
   records. Encrypted fields are opaque outside the user's devices.
5. **Platform bridge**: narrow Objective-C++ helpers for AppKit menus, gestures,
   default-browser registration, Keychain/LocalAuthentication, and optional
   `NSGlassEffectView` material.

## Profile and window invariants

| Surface | Chromium context | Persistent | Normal cookies/extensions | Synced |
| --- | --- | --- | --- | --- |
| Workspace window | shared normal profile | yes | yes | permitted UI records |
| Quick Window | shared normal profile | window no; resulting tab configurable | yes | resulting normal records |
| Incognito window | off-the-record profile | no | no | never |
| iOS companion | no browser context | permitted local records | not applicable | permitted records |

## Tree model

Folders may contain folders or page nodes without an artificial depth limit.
A page node can be saved/persistent or temporary. Opening a page creates or
activates a Chromium `WebContents`; closing a temporary page removes the node,
while closing a saved page only releases its live `WebContents`. Tree identity
is independent from transient `SessionID` values.

The controller must tolerate renderer crashes, discarded tabs, restored
sessions, extension-created tabs, popups, and tabs moved between windows.

## UI technology

Desktop browser chrome is Chromium Views. Native macOS effects use bounded
Objective-C++ bridges. Glass is optional, respects accessibility/reduced
transparency, and cannot reduce contrast or become a layout dependency.
Desktop strings use GRIT/ICU. The companion uses Apple string catalogs. Theme
tokens support system/light/dark, one global accent, and workspace overrides.

## Patch strategy

The repository never mirrors Chromium. `patches/chromium/series` orders small
patches applied to a separately managed pinned checkout. New standalone code
lives under `overlay/chromium/`. The overlay and complete ordered patch series
are first composed in a temporary Git index, validated as one delta, and only
then applied to the checkout. A failed composition leaves Chromium pristine,
and dependent patches are evaluated in series order. Each patch has an entry in
`patches/chromium/README.md` with owner, affected upstream paths, rationale,
tests, and expected rebase risk.
