# UI system

AhoiBrowser's chrome should feel native, calm, and deliberate. It borrows Arc's
spatial clarity, not its complete visual identity. The browser is one content
surface plus a vertical organization surface; it does not add a second horizontal
tab strip or permanent bookmark bar.

## Layout

The left sidebar owns workspace switcher, unlimited tree, temporary tabs,
downloads/activity, and bottom utility controls. It can collapse completely and
resizes within the limits in `config/theme.json`. Page content always receives
the remaining native window space; overlays cannot permanently reduce its frame.

Its information hierarchy stays Arc-like without copying Arc pixel for pixel:
only the compact workspace identity sits above saved groups/pages; a quiet
divider and clear-temporary action precede adjacent temporary tabs; and a fixed
four-action dock for New Tab, Downloads, History, and Settings remains pinned
below the scroll surface. A newly created blank tab is a normal temporary row
with a plus icon and localized New Tab title. Rows are flat by default and gain
rounded surfaces only for hover, selection, drag targets, split membership, or
an explicitly colored group.

The first native integration slice starts from Chromium M151's existing Views
vertical tab strip. Ahoi enables its full-launch feature and vertical-mode
profile preference by default. The existing preference/controller toggle stays
authoritative, so a user can switch back to Chromium's horizontal strip and a
feature override can still disable the vertical surface entirely. Nested Ahoi
tree semantics are deliberately outside this bootstrap slice.

Tree rows use progressive indentation, disclosure state, favicon, title, optional
activity chip, and restrained hover/selection treatments. Deep nesting remains
keyboard navigable and virtualized. Drop targets communicate before/after/inside
precisely, auto-expand deliberately, and never depend on color alone.

Folders persist an optional icon and semantic color accent. A colored folder
draws one continuous, contrast-safe group bubble around its visible direct
children; nested folders form visually distinct nested bubbles instead of
breaking the hierarchy into unrelated cards. Collapse, virtualization, split
segments and drag previews preserve that visible membership. Uncolored folders
remain on the flat semantic surface.

Tab drag and drop is a complete interaction, not a later enhancement. Row-edge
targets reorder, folder-center targets nest, and dropping one page on the center
of another page creates a real split. The preview names the exact operation and
Escape cancels it atomically. Files, folders, multi-selections, and invalid
profile combinations cannot be mistaken for a split gesture.

## Split view

Two, three, or four normal Chromium `WebContents` can be visible and live at
once.
Two-pane layouts are columns or rows. Three-pane layouts include three columns,
three rows, and the four asymmetric main-pane arrangements. Layout IDs, ratios,
limits, and persistence rules are defined in `config/split-view.json`; detailed
interaction and security behavior is defined in `docs/SPLIT_VIEW.md`.

Each pane has compact browser-owned origin and activity state, while one clear,
non-color-only outline identifies the pane controlled by the address bar,
navigation buttons, Page Info, permissions, extensions, and developer actions.
Clicking a pane changes focus without reloading it. Dividers support pointer and
keyboard resizing, accessible values, snap points, and safe minimum sizes.

A page dropped onto an existing two-pane group previews where it will become
the third pane. A fourth pane joins as a visible `four-grid` 2×2 preview. A
fifth pane is rejected visibly and never replaces or closes
content. Dragging a pane back to a normal sidebar target removes it from the
group. Browser fullscreen retains the layout; content fullscreen temporarily
shows its originating pane and restores the exact layout afterward.

## Command bar

`Command+L` and `Command+T` focus the same centered command surface with different
initial intent. It ranks exact URL/navigation, open tabs, saved tree pages,
folders/workspaces, history, search, and browser/developer commands. Results have
clear type labels and one keyboard selection model. `g ` forces Google search.
The surface is fast without a remote service or model dependency.

## Workspaces and gesture

Workspace changes are interactive: sidebar content follows the horizontal Magic
Mouse gesture and settles or cancels based on distance/velocity. Reduced Motion
uses a short cross-fade. Gesture direction, sensitivity, and disable control are
settings. Keyboard/sidebar switching remains first-class and deterministic.

## Appearance and glass

System/light/dark resolve into semantic tokens rather than hardcoded per-view
colors. One global accent has optional workspace overrides. Native glass is
limited to browser chrome and short-lived overlays through `NSGlassEffectView`;
web content is not sampled or blurred. Reduced Transparency uses opaque semantic
surfaces. Increased Contrast and focus rings must remain legible in every accent.

## Accessibility and localization

Every operation is keyboard reachable. Views expose roles, labels, state,
hierarchy, set membership, expanded/collapsed state, drag/drop announcements, and
focus order to macOS accessibility APIs. Split groups expose pane count and
layout; each pane exposes title, origin, position, activity state, and focused
state; every divider exposes an adjustable value. German and English layouts
are both designed, not just translated; no fixed text widths, raw keys, or
concatenated sentences. Desktop strings ultimately live in GRIT/ICU and
companion strings in Apple string catalogs.

## Convenience without clutter

Frequently used actions appear contextually: cache/site-data reset, cookie
inspection, CSS/JS editor, header rules, HTTP-auth account switch, Picture in
Picture, permissions, and DevTools. Advanced or dangerous state gets a visible
activity chip and one-action reset. Screenshot, AI, broad ad blocking, boosts,
promotional sharing, and duplicate developer surfaces are outside the core.
