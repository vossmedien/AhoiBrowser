# Architecture

## System shape

```text
AhoiBrowser Views/AppKit chrome
  -> WorkspaceTreeController (Ahoi-owned UI/session model)
  -> SplitViewService (Ahoi-owned policy/session coordinator)
  -> Chromium Browser + TabStripModel
       -> SplitTabCollection (two to four normal tabs)
  -> Profile / OffTheRecordProfile
  -> WebContents
  -> Chromium services
       extensions | downloads | history | passwords | permissions
       media | DevTools | network service | session restore | sync adapter

AhoiBrowser Mobile (SwiftUI + system WebKit, iOS/iPadOS 26+)
  -> MobileBrowserController -> WebPage/WebView -> WKWebsiteDataStore
  -> WebKit dialogs/permissions/WKDownload + native origin-labelled surfaces
  -> local normal-session store + local-first encrypted record store
  -> event-driven CKSyncEngine private-database adapter
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

## macOS update boundary

`ahoi/browser/updater` is a narrow Objective-C++ adapter around the pinned
official Sparkle framework. Ahoi owns configuration validation, channel policy,
native menu/settings presentation, localized status and accessibility
announcements. Sparkle alone owns network checks, download, delta/full fallback,
Ed25519 verification, extraction, atomic install and relaunch. The adapter never
implements an alternate installer or archive parser.

The runtime starts only after a credential-free HTTPS feed, 32-byte public
Ed25519 key, exact framework pin, signed-feed enforcement and verification before
extraction have all passed. Stable, beta and nightly visibility is monotonic, but
the signed build's feed/key trust tuple is immutable. Release tooling binds the
signed appcast to the release manifest, materials receipt, SBOM/license evidence
and exact Sparkle upstream artifact. See `docs/UPDATES.md`.

## Profile and window invariants

| Surface | Chromium context | Persistent | Normal cookies/extensions | Synced |
| --- | --- | --- | --- | --- |
| Workspace window | shared normal profile | yes | yes | permitted UI records |
| Quick Window | shared normal profile | window no; resulting tab configurable | yes | resulting normal records |
| Incognito window | off-the-record profile | no | no | never |
| iOS/iPadOS Mobile normal | system WebKit persistent store | normal session records | WebKit-owned | permitted UI/history/tab records |
| iOS/iPadOS Mobile private | nonpersistent WebKit store | no | isolated | never |

Mobile normal tabs use WebKit's persistent default store. All private tabs in
the current private session share one nonpersistent store and never enter normal
session/history/search/sync records. `WKDownload` remains the transfer engine;
Ahoi owns safe destination naming and native progress/preview/share UI. Explicit
private downloads remain persistent user output. Permission, JavaScript,
file-input and external-scheme prompts retain initiating-origin attribution and
do not create a second persistent permission authority.

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
the runtime source of truth. A split group contains exactly two, three, or four
tabs, one focused pane, a bounded layout tree, and one or two divider ratios.
The canonical layouts and limits live in `config/split-view.json`; the complete
behavioral contract lives in `docs/SPLIT_VIEW.md`.

Creating a split changes presentation only. It does not implicitly save,
delete, reorder, or re-parent `TreeNode` records. Topology belongs to a normal
window/workspace session, persists through Session Service and crash restore,
and is never CloudKit-synced. Off-the-record topology stays in memory and is
never restored. A failed operation is atomic; a missing restore leaf degrades
four panes to three, three panes to two, or two panes to one without phantom
tabs.

Chromium Stable M152 supplies the upstream two-pane seams: split collections
inside the normal tab hierarchy, `MultiContentsView` and
`ContentsContainerView`, vertical split rows, focus/security attribution,
resizing, drop targets, Session Restore, Tab Restore, and extension `splitId`.
The active M152 Ahoi integration patch generalizes those seams to up to four
children and a layout tree. Hard-coded two-child assumptions in
creation/restore APIs, visual data, Views layout, menus, utilities, metrics,
serialization, and tests must be removed together. A parallel Ahoi-specific pane controller is prohibited.

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

For the active Chromium M152 pin, the ordered series contains exactly four
entries: one consolidated integration-seam patch, one deterministic-platform
test patch, one upstream page-load tracing-isolation patch, and one bounded
Lean-profile Compose-guard patch. The former 21-entry M151 stack is preserved
through its recovery evidence, not kept active as duplicated maintenance
surface.

Within the M152 integration-seam patch, split view preserves upstream two-pane
behavior, connects Ahoi sidebar drag targets, introduces versioned
two-/three-/four-pane visual data and serialization, and generalizes the model
and Views layout. Accessibility, security, restore, extension, media, DevTools,
and installed Computer Use coverage remain explicit gates. The implementation
keeps a working two-pane fallback and an explicit migration test.
