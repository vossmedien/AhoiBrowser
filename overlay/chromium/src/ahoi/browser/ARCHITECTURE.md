# Ahoi browser-layer architecture

Ahoi extends Chromium at explicit browser-layer seams. Upstream Chromium files
should receive the smallest practical integration patch; Ahoi-owned behavior
lives below `//ahoi/browser` so Chromium updates remain reviewable.

## Module boundaries

- `tab_tree/` owns durable workspace, folder, saved-page and undo state. Any
  multi-row user action must expose one atomic store operation.
- `navigation/` owns workspace activation and command-domain state without
  depending on Views.
- `session/` binds durable Ahoi identities to Chromium windows and live tabs.
  Its split translation units separate lifecycle, workspaces, runtime tabs and
  observers.
- `resource_policy/` projects Chromium lifecycle state, applies short-lived
  safety holds for product-critical flows and exposes manual sleep/wake. It
  never schedules discards: MemorySaverModePolicy and PageDiscardingHelper
  remain the timing, eligibility and discard authorities.
- `ui/sidebar/` separates store mutations (`sidebar_tree_controller*`), the
  virtualized projection (`sidebar_tree_view*`), row painting, runtime-tab
  views, drop targets, dialogs and host coordination.
- `command_bar/` owns the Cmd+T presentation and delegates browser mutations to
  its execution adapter.
- `http_auth/` owns Basic Auth credential metadata, protection-space matching
  and PasswordStore integration.
- `popup/` owns overlay eligibility and the lifetime of the one real Chromium
  `WebContents`; `ui/popup/` owns browser-controlled origin chrome, focus and
  the adapter into normal tabs, native popup windows and Chromium split tabs.

Production translation units should normally stay below 800 lines. A file is
split by responsibility, not through textual `.inc` fragments. Tests may share
small typed fixtures, but production state has one owner.

## Visual system and themes

`//ahoi/browser/ui:visual_style` is the only source for shared semantic colors,
spacing, radii, component metrics and motion durations. Ahoi colors resolve
through Chromium's `ui::ColorProvider`, which preserves native light, dark,
high-contrast and installed-theme behavior. Component code should not embed a
dark palette or copy shared dimensions.

User-selected primary colors can later be supplied by an Ahoi ColorMixer at
the same semantic token boundary. That must not require rewriting Sidebar,
Cmd+T, dialogs or hover/drag states. macOS glass materials are likewise a
platform rendering option behind the shared surface tokens, not a second set
of components.

Motion is presentation state, never domain state. Expand/collapse, overlay and
hover transitions must be short, interruptible and driven by the shared motion
tokens. They must finish immediately when Chromium reports reduced motion, may
not delay a tree mutation or command, and may not synchronously relayout the
sidebar while macOS is starting a native drag session.

## Native interaction rule

Programmatic store/model tests do not prove native Views behavior. Changes to
drag and drop, focus, context menus, overlays, split views or window layout
must also be exercised in a freshly built `.app` through the actual macOS
pointer and keyboard path.
