# Architecture

## System shape

```text
AhoiBrowser Views/AppKit chrome
  -> WorkspaceTreeController (Ahoi-owned UI/session model)
  -> SplitViewService (Ahoi-owned policy/session coordinator)
  -> Chromium Browser + TabStripModel
       -> SplitTabCollection (two or three normal tabs)
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
surfaces, split-view policy and local topology persistence, developer
convenience orchestration, theme tokens, localization, and the CloudKit record
adapter.

No Ahoi service may create an alternate cookie jar, password database, download
stack, permission store, media stack, renderer, or authentication protocol.

## Desktop layers

1. **Ahoi Views UI**: sidebar/tree, toolbar, command bar, settings, permission
   indicators, download surfaces, extension controls, and developer tools.
2. **Browser integration**: maps tree leaves to `WebContents` and normal
   `TabStripModel` lifecycle without replacing Chromium navigation semantics.
   Split membership extends Chromium's existing split collection and never
   creates an alternate `WebContents` host.
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

## Split-view model

`SplitViewService` coordinates UI policy and persistence, but Chromium's
`TabStripModel`, `SplitTabCollection`, `TabInterface`, and `WebContents` remain
the runtime source of truth. A split group contains exactly two or three tabs,
one focused pane, a bounded binary layout tree, and one or two divider ratios.
The canonical layouts and limits live in `config/split-view.json`; the complete
behavioral contract lives in `docs/SPLIT_VIEW.md`.

Creating a split changes presentation only. It does not implicitly save,
delete, reorder, or re-parent `TreeNode` records. Topology belongs to a normal
window/workspace session, persists through Session Service and crash restore,
and is never CloudKit-synced. Off-the-record topology stays in memory and is
never restored. A failed operation is atomic; a missing restore leaf degrades
three panes to two or two panes to one without phantom tabs.

Chromium M151 already supplies the correct two-pane seams: split collections
inside the normal tab hierarchy, `MultiContentsView` and
`ContentsContainerView`, vertical split rows, focus/security attribution,
resizing, drop targets, Session Restore, Tab Restore, and extension `splitId`.
Ahoi generalizes those seams to three children and a layout tree. Hard-coded
two-child assumptions in creation/restore APIs, visual data, Views layout,
menus, utilities, metrics, serialization, and tests must be removed together;
shipping a parallel three-pane controller is prohibited.

Exactly one pane is active. Browser-mediated UI and sensitive actions are
attributed to it, while every visible pane keeps a browser-owned origin and
media/capture indicator. Inactive panes remain live, but cannot create a new
permission prompt or system file picker until focused. Site Isolation,
sandboxing, storage partitions, and per-`WebContents` DevTools remain upstream
Chromium responsibilities.

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

Split view is staged as reviewable patches: enable and preserve upstream
two-pane behavior; connect Ahoi sidebar drag targets; introduce versioned
two/three-pane visual data and serialization; generalize model/view/layout;
then add macOS interaction, accessibility, security, restore, extension, media,
DevTools, and installed Computer Use coverage. Every stage keeps a working
two-pane fallback and an explicit migration test.
