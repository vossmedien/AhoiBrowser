# ADR 0012: Merging Workspaces, folder moves by command, recent-tabs flick and mobile Web Extensions

Status: accepted 2026-09-26 (user decision on
[handoff 074](../../handoffs/crest-hardening/074-reddit-crest-post/HANDOFF.md),
from a user post in r/CrestBrowser: "ja bitte, ins Hauptziel mit aufnehmen und
umsetzen"). Extends the master contract (Workspaces, Command Bar, mobile
companion), ADR 0002 and ADR 0011; replaces the mobile contract's exclusion of
extensions only for WebKit Web Extensions (section 4). Contract, catalogue and
review: lane `crest-hardening` (package H7). Implementation: desktop and mobile
owners; sync format: sync lane.

## 1. Merge Workspaces (desktop and mobile)

- Every Workspace has the action "Zusammenführen mit …" (context menu, command bar;
  mobile: Workspace detail). The user picks the target B for source A.
- Default: A's unpinned top-level nodes move, in their order, into **one new
  folder at the end of B**. The folder is named after A and keeps A's icon and
  color if B's folders can show them. Option "Ohne Ordner" appends them flat.
  (The tree has no separate pin area on either platform; should one come, A's
  pins join B's.) Folders, saved pages, saved start addresses, archive entries
  and split groups keep their IDs and structure.
- A is then removed. A's Workspace settings (routing rules, theme, action pins)
  are not merged: B's win, and routing rules that pointed at A point at B.
- One structure transaction under the single-writer rule (H2): all moves and
  A's removal commit together or not at all. Undo (toast plus ⌘Z on desktop,
  toast on mobile) restores A with its ID, name, settings and nodes.
- Open tabs keep running when both Workspaces use the same web context
  (`shared` with `shared`, ADR 0002); such a merge can be undone.
- Otherwise (A or B has its own website sessions), A's saved pages and folders
  still move, but an open tab can't change its storage partition:
  - A's open tabs are asked as one before-unload group, as when deleting A.
    A veto changes nothing; after agreement they close.
  - A's own website sessions are retired and cleared like on deletion.
  - The dialog says so, and this merge has no undo, because the retired
    sessions can't come back.
  - Nothing ever merges two profiles or partitions.
- A `Vollständig getrennt` Workspace (its own profile) is not a merge
  partner in the first step: the menu only offers Workspaces of the same
  profile. Merging across profiles would follow the conversion path of
  ADR 0011 (portable structure, pages reopened in the other profile) and is
  a later step (`WS-MERGE-04`).
- Confirmation names what happens: the number of pages and folders, the target,
  and for different levels that logins stay behind. There is no confirmation
  when A is empty.
- Sync: a merge is node moves plus A's tombstone. The tombstone carries
  `mergedInto: B` so a peer that adds a node to A concurrently re-homes it into
  B instead of losing it or re-homing it to the default Workspace. The sync lane
  decides the wire field (ADR 0009 format) and adds conformance vectors (H1).
  Peers without the field fall back to the existing deletion re-homing (R6).

## 2. Move folders between Workspaces by command

Moving folders already works by context menu and drag and drop (desktop) and
by the move sheet (mobile). The command bar gets "In Workspace verschieben …"
for the focused sidebar node or the current tab (folder, page or split group),
with the same targets and behavior as the context menu. It is also findable
by typing the Workspace name after the command.

## 3. Mobile: flick through recent tabs

The mobile contract's recent-tabs flick ("Schneller Tabwechsel") becomes
a v1 requirement. A one-finger horizontal swipe on the Harbor deck's control row
(address field) switches to the previous or next tab in most-recently-used order
within the current Workspace. Today `switchSelectedTab` steps by list position
and there is no MRU list; the flick orders by `lastActiveAt` and keeps that
order fixed while the user keeps flicking. It shows the neighbor page's preview during the drag
(Safari convention), with haptics at the commit point. The web view's edge swipe
for page history stays untouched: the flick is only recognized on the bar,
never over web content. The existing Workspace swipe on the deck's top rail
(`MobileHarborDeckView`) stays; the control row is visible even when the deck
is collapsed. VoiceOver gets "Vorheriger Tab"/"Nächster
Tab" actions. Two- or three-finger gestures are not used: iOS reserves them
(text editing undo/redo, VoiceOver).

## 4. Mobile Web Extensions (WebKit)

- Mobile supports WebKit Web Extensions through `WKWebExtensionController`
  (iOS 18.4 or later). Chromium extensions stay desktop-only.
- Plan of record for the runtime: `outputs/AhoiBrowser-Mobile-uBlock-Feasibility.md`
  (`MobileWebExtensionRuntime`, `MobileWebExtensionHost`). Mobile has no content
  blocker and no extension code yet; the deployment target is iOS 26.
- Step 1, spike (time-boxed, mobile owner): first check whether SwiftUI's
  `WebPage.Configuration` (`makePage`, `MobileBrowserControllerWebPageLifecycle.swift`)
  can carry a `WKWebExtensionController`, or which WebKit bridge is needed. Then load one MV3 extension from the
  app bundle and one unpacked from Files into the controller, attached to
  every normal-browsing `WebPage`/`WKWebView` configuration. Private browsing
  is off by default per extension. Check content scripts, `declarativeNetRequest`,
  popup/action, storage, permission prompts, the App Store review guideline
  2.5.2 on executable code, the privacy manifest and the update path. Record
  the result as evidence; the user decides on step 2 from it.
- Step 2, v1 scope if the spike passes: "Erweiterungen" in the mobile settings with
  install from Files (unpacked folder or `.zip`), enable, disable, remove, a
  per-site permission prompt, the action in the page menu, and a private-browsing
  toggle per extension. There is no store, no sync of extensions between devices,
  and no reading of Safari's installed extensions (third-party apps can't).
- Licenses: a bundled extension needs the same file-level license and notice
  check as desktop uBlock Origin; nothing is bundled before that check.

## Acceptance cases (catalogue, see the H7 section of the lane goal)

- `WS-MERGE-01`: merge shared A into shared B with a folder, a split
  and an open tab. Result: one folder "A" in B, the split
  intact, the tab still running, A gone; after a restart, the same.
- `WS-MERGE-02`: "Ohne Ordner" appends flat, in order.
- `WS-MERGE-03`: undo restores A with the same ID, name, settings and nodes, and the
  tabs back in A.
- `WS-MERGE-04`: A with its own website sessions into shared B. The saved pages
  arrive in B; A's open tabs are asked with before-unload and close; A's
  binding and partition data are removed; no cookie from A appears in B; the
  dialog says the merge can't be undone. (Across profiles: later step.)
- `WS-MERGE-05`: a crash between the moves and A's removal leaves either the
  whole merge or none of it (transaction).
- `WS-MERGE-06`: sync. Device 1 merges A into B while device 2 adds a page
  to A offline; after both sync, the page is in B on both devices.
- `WS-MERGE-07`: mobile merge with the same results as 01–03.
- `CMD-MOVE-01`: command bar "In Workspace verschieben …" moves the focused
  folder, including its children, into the chosen Workspace; undo works.
- `MOB-FLICK-01`: a swipe on the bottom bar switches to the last-used tab and back;
  the edge swipe over the page still goes back in history.
- `MOB-FLICK-02`: VoiceOver actions switch tabs; there is no flick in a Workspace with
  one tab.
- `MOB-EXT-01` (spike): a bundled MV3 test extension injects a content script
  and blocks a request through `declarativeNetRequest`.
- `MOB-EXT-02` (v1): install from Files, enable, disable, remove; the extension is
  off in private browsing until allowed.
- `MOB-EXT-03` (v1): the permission prompt per site; denial keeps the page unchanged.
