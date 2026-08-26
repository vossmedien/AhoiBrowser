# Split view

Split view is a release-critical part of AhoiBrowser's tab model. A user can
drag one page row onto another page row in the left sidebar and keep two,
three, or four real pages visible at the same time. Every pane remains a normal
Chromium `WebContents`; split view is not a screenshot, preview, embedded web
app, or parallel WebView host.

The machine-readable limits and layout names are authoritative in
`config/split-view.json`. This document defines their behavior.

## Chromium M151 integration map

The pinned Chromium revision `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`
already contains a production-quality two-tab foundation:

- `chrome/browser/ui/tabs/tab_strip_model.{h,cc}` owns `AddToNewSplit`, split
  layout/ratio updates, reverse, removal, focus, detach/attach, and observer
  events.
- `components/tabs/public/split_tab_collection.h` (`SplitTabCollection`) and
  `components/tabs/impl/tab_strip_collection.cc` keep two through four split
  tabs as one real collection inside the normal tab-strip hierarchy.
- `components/split_tabs/split_tab_visual_data.{h,cc}` stores orientation,
  three-pane arrangement, and primary/secondary divider ratios.
- `chrome/browser/ui/views/frame/multi_contents_view.{h,cc}` (`MultiContentsView`) owns four bounded
  `ContentsContainerView` instances, focus, resizing, drop targets, canonical
  layouts, and accessibility pane enumeration.
- `chrome/browser/ui/views/frame/contents_container_view.{h,cc}` keeps a real
  content surface, docked DevTools, tab-modal host, security outline, capture
  indicators, and the per-pane mini toolbar together.
- `multi_contents_view_drop_target_controller.*` and
  `multi_contents_view_delegate.*` accept a single dragged tab or safe link and
  create a split through `TabStripModel`.
- `chrome/browser/ui/views/tabs/common/split_tab_view.*` already represents a
  split collection in Chromium's vertical tab strip, while the common tab-drag
  controller already reorders vertical tabs and whole split collections.
- `SessionServiceBrowserHelper`, `SessionService`, `session_restore.cc`, and
  Tab Restore persist two-pane IDs, orientation, ratio, and membership.
- Chromium's split security model already requires an unmistakable active-pane
  outline, per-pane origin/media indicators, active-pane attribution for
  browser UI, and suppression of permission prompts and file pickers from an
  inactive pane.

This is the seam AhoiBrowser extends rather than reimplementing. Its bounded
generalization removes M151's hard two-item assumptions from creation,
restoration, visual data, utilities, menus, and content layout while retaining
the upstream ownership model. The macOS upstream tab-to-content drag UI test is
disabled in M151, so Ahoi also carries its own interaction coverage and still
requires installed Computer Use proof.

## Model and ownership

`SplitViewService` is a browser-level policy and persistence coordinator. It
does not own an alternate tab store. `TabStripModel`, its split collection, and
`tabs::TabInterface` remain authoritative for membership and lifecycle; every
leaf maps one-to-one to a normal `WebContents`.

A split group has:

- a stable split-group UUID plus Chromium's runtime split identifier;
- one window and one workspace-session owner;
- an ordered set of exactly two, three, or four tab handles;
- a canonical layout tree and primary/secondary divider ratios;
- exactly one focused/active pane;
- timestamps needed for atomic session persistence.

Membership is runtime presentation state. Creating a split does not silently
move, save, delete, or re-parent sidebar tree nodes. Saved pages remain saved;
temporary tabs remain temporary. A separate before/after/inside drop performs a
tree move. Normal split topology is device-, window-, and workspace-session
local and is not CloudKit-synced. Incognito topology never leaves memory.

## Canonical layouts

Two-pane groups support:

- `two-columns`: start/end, mirrored visually for RTL;
- `two-rows`: top/bottom.

Three-pane groups support:

- `three-columns` and `three-rows`;
- `main-left` and `main-right`, with the opposite column split into two rows;
- `main-top` and `main-bottom`, with the opposite row split into two columns.

Four-pane groups support `four-grid`, a row-ordered 2×2 layout. The primary
ratio sizes the two columns in side-by-side orientation beziehungsweise the
two rows in stacked orientation. The secondary ratio sizes the shared cross
axis, so both rows/columns remain aligned. Both ratios survive session restore,
browser restart, crash recovery, and supported layout changes.

The representation is a bounded layout tree with two to four leaves,
not ad-hoc rectangles. The root divider uses the primary ratio and the nested
divider uses the secondary ratio. Reordering panes changes leaf order without
recreating their `WebContents`. Changing a layout preserves the focused pane
and uses the nearest meaningful prior ratios.

Dividers are mouse-, trackpad-, and keyboard-resizable, expose accessible
values, and snap to the configured points. The preferred minimum is 240 DIPs
per pane. Existing groups may compress to the constrained minimum of 96 DIPs in
a narrow window, but panes may not silently disappear or overlap. Adding a
third pane is disabled when the resulting bounds cannot satisfy constrained
minimums; the UI explains why.

The same minimum-size rule applies when adding the fourth pane and never
silently replaces an existing pane. The sidebar renders one coherent split
item whose segment geometry mirrors the content layout. In particular,
`four-grid` appears as a compact 2×2 segment grid in row order. It may use a
controlled taller row for legibility, but Favicons, titles, media state, focus,
close actions, drag targets, and accessibility labels remain individually
addressable.

## Sidebar drag and drop

Every drag has a single previewed outcome before mouse-up:

- the top and bottom edge of a page row mean `before` and `after` reorder;
- the center of a folder means `inside` and moves the node into that folder;
- the center of a page row means `split with this page`;
- a dragged folder, multi-selection, file, or other non-tab payload cannot
  accidentally create a split;
- Escape, pointer cancellation, a rejected target, or a failed tab detach is
  atomic and leaves both the tree and split topology unchanged.

Dropping one page onto a normal page creates the default two-column layout.
The preview exposes a direct two-row alternative before commit, and the layout
menu remains available afterward. Dropping one page onto any pane of a
two-pane group previews the exact three-pane insertion and layout. Dropping a
page onto a three-pane group previews the exact four-pane insertion and
defaults to `four-grid`. Dropping an external page onto a full four-pane group
is rejected with a visible
explanation; it never closes, replaces, or hides a page. Dragging a pane within
its own group reorders leaves or selects another layout. Dragging it to a normal
before/after/inside target removes it from the group; two remaining panes stay
split, while a one-pane remainder collapses to a normal tab. Removal degrades
in the order `4 -> 3 -> 2 -> 1` with a valid balanced layout at every step.

Cross-window drag uses Chromium's detach/insert path and transfers the actual
tab, including navigation history and live page state, before atomically
creating the target split. A normal and an `OffTheRecordProfile` tab can never
share a group. Quick Window may offer `Move to browser and split`, but it does
not host multi-pane UI itself.

Link drops into a content-edge target may create a new temporary tab through
Chromium's safe URL filtering. `javascript:` and other blocked URLs remain
blocked. File drag/drop retains its upload/navigation meaning and cannot be
misclassified as tab-splitting.

## Focus, navigation, and security attribution

Only one pane is active, even though every pane is visible and live. Clicking,
tabbing into, or explicitly focusing a pane activates its existing
`TabInterface`; it does not reload the page. The address bar, back/forward,
reload, Page Info, permission surfaces, downloads, developer commands, and
extension actions always target that active pane.

Every pane shows enough browser-controlled origin and security state to prevent
one visible site being mistaken for another. The active pane has a persistent,
non-color-only outline that becomes stronger while the omnibox, Page Info,
permission prompt, device chooser, or other security-critical browser UI is
open. Tab-modal dialogs and scrims stay within their originating pane. An
inactive pane cannot open a permission prompt or system file picker; focusing
it allows the pending request to proceed with clear attribution. Same-origin
policy, Site Isolation, renderer sandboxing, storage partitions, and permission
decisions are unchanged.

Keyboard equivalents exist for every drag-only action: `Split with…`, add the
focused tab as pane, choose layout, move focus to next/previous pane, reorder
pane, resize the selected divider, remove focused pane from split, and close
split. Shortcut defaults must avoid Chromium, macOS, extension, and developer
tool conflicts and remain configurable.

The shipped macOS defaults are defined in `config/shortcuts.json` and handled
by the native browser event path:

- `Command+Control+1…4` focuses a pane without reloading it;
- `Command+Control+Shift+Left/Right` reorders the focused pane;
- `Command+Control+L` cycles the layouts valid for the current pane count;
- `Command+Control+Left/Right` changes the active divider;
- `Command+Control+0` separates the split; and
- `Command+Control+W` closes only the active pane through Chromium's normal
  close and before-unload path.

The split menu exposes the same layout and separation operations. Pointer-only
drag behavior is therefore never the sole way to create, arrange, resize, or
leave a split.

For three panes the menu exposes checked presets for three columns, three rows,
main-left, main-right, main-top, and main-bottom. For four panes it exposes the
two persisted 2x2 traversal orientations. Applying a preset updates only the
visual layout and balanced ratios; it does not replace or navigate any
`WebContents`.

## Media, Picture in Picture, downloads, and permissions

All visible panes remain live and report `visibilityState=visible` while their
group is active. Audio or video in one pane is not paused merely because focus
moves to another pane. Each page row and pane toolbar shows its own playing,
muted, camera, microphone, capture, and sharing state. Muting is per tab;
browser-level media controls continue to follow Chromium Media Session/audio
focus behavior.

Video Picture in Picture starts from the originating `WebContents` and survives
pane focus, layout, divider, workspace, window-minimize, and sidebar changes in
the same way as a normal tab. Ahoi does not promise multiple simultaneous PiP
windows when the upstream platform permits only one. Closing or navigating the
origin uses Chromium's normal PiP lifecycle.

Downloads, uploads, authentication prompts, and permission requests retain the
originating `WebContents` and origin. Progress remains global in the download
surface, while the initiating pane is identifiable. An inactive pane may keep
an already granted camera/microphone/WebRTC stream alive and visible, but it
cannot surface a new sensitive prompt without activation.

## DevTools

DevTools opens for the focused pane and stays bound to that exact
`WebContents`; later focus changes do not silently retarget it. A detached
DevTools window works unchanged. Docked DevTools uses the originating
`ContentsContainerView`, so it resizes inside that pane rather than replacing
another pane or the entire split. If a three- or four-pane layout becomes too
narrow,
the UI offers undocking or resizing and never silently closes DevTools. One
docked DevTools instance per pane is supported, subject to the normal resource
and minimum-size rules.

## Close, fullscreen, crash, and restore

Closing one pane closes only its tab using Chromium's normal before-unload and
history behavior. Removing a pane from a split keeps the tab open. Closing the
whole group is an explicit action and participates in Tab Restore. A two-pane
group becomes one normal tab when one member is closed; a four-pane group
becomes a corresponding three-pane layout and a three-pane group becomes the
corresponding two-pane layout without reloading any survivor.

Browser fullscreen keeps the complete split visible. Tab/content fullscreen
temporarily presents only the requesting pane and restores the exact group,
layout, ratios, and focus on exit. PiP remains independent.

A renderer crash affects only its pane and shows the normal Sad Tab surface;
other panes remain interactive. Normal browser-session restore persists
membership, layout tree, ratios, and focused pane atomically. If a leaf cannot
be restored, a four-pane group degrades to a valid three-pane layout, a
three-pane group degrades to the matching two-pane layout, and a two-pane group
degrades to one normal tab. It never creates a phantom tab or
loses a restorable survivor. Browser-process crash recovery follows the same
rule. Off-the-record groups are never serialized, shown in restore UI, synced,
or sent to the companion app.

## Accessibility and localization

The sidebar announces reorder, folder nesting, proposed split position,
accepted split, cancellation, and rejection. Split groups expose their group
name, pane count, layout, focused member, and ordered membership. Each content
surface is labelled `Pane 1 of 4, <title>, <origin>` (localized); dividers expose
orientation, current percentage, minimum, maximum, and keyboard adjustment.
Focus order follows visual order in LTR and RTL without making security
attribution depend on color.

All menu items, drag announcements, rejection reasons, pane labels, and layout
names use GRIT/ICU and ship in German and English. Pseudolocalization, 200%
text/zoom, Reduced Motion, Increased Contrast, and Reduced Transparency are
part of the release test matrix.

## Verification boundary

Chromium unit/browser tests must cover collection membership, two-to-three,
three-to-four, four-to-three, and three-to-two transitions, layout
normalization, 2×2 geometry and both persisted ratios, focus, security
attribution,
session serialization/migration, extension API behavior, invalid drops, and
crash degradation. Views interaction tests must cover vertical-sidebar drag
targets and dividers on macOS. None of that replaces visible Computer Use tests
against the signed app installed at `/Applications/AhoiBrowser.app`.

All `SPLIT-*` IDs in the master target and `config/test-registry.json` are
release-critical. The upstream M151 feature alone does not satisfy them.
