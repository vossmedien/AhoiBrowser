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
focus order to macOS accessibility APIs. German and English layouts are both
designed, not just translated; no fixed text widths, raw keys, or concatenated
sentences. Desktop strings ultimately live in GRIT/ICU and companion strings in
Apple string catalogs.

## Convenience without clutter

Frequently used actions appear contextually: cache/site-data reset, cookie
inspection, CSS/JS editor, header rules, HTTP-auth account switch, Picture in
Picture, permissions, and DevTools. Advanced or dangerous state gets a visible
activity chip and one-action reset. Screenshot, AI, broad ad blocking, boosts,
promotional sharing, and duplicate developer surfaces are outside the core.
