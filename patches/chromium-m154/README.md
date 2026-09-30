# Staged M154 patch stack (not active)

This directory holds the patch stack rebased onto Chromium Stable
`154.0.8037.93` (`f89f3a4363808e117c592adedcf9947882ac3b79`, bound in
`config/upstream-roll-candidate.json`). It is **not** used by any build:
`config/chromium.json` still pins M153, and `apply-overlay.sh`,
`compose_overlay.py`, `chromium_roll.py`, the overlay fingerprint and the
source line budget read only `patches/chromium/`. Nothing here is compiled
or tested yet.

At the roll commit (roll policy step 3 in `docs/UPSTREAM.md`) the files
replace `patches/chromium/*.patch` and `patches/chromium/series`, and this
directory is removed.

## Provenance

- Discovery 30 Sep 2026 08:40 UTC: the only pinnable, fully rolled Mac ARM64
  Stable record; Chromium Dash, VersionHistory and the Gitiles tag agree on
  the commit.
- Preflight of the M153 stack on M154
  (`artifacts/build/chromium-roll-preflight-m154.json`, local): 22 apply,
  62 conflict, 0 already upstream. Overlay path collisions: 22 (the same
  icon files as on M153).
- Rebase: an isolated sparse Git repository (`.work/chromium-m154-rebase`,
  object store borrowed read-only from `.work/chromium/src`) with the
  patch-touched paths of M153 and M154; every patch was cherry-picked in
  series order as its own commit (branch `m154`, base `m154-base`). The
  active M153 checkout and `out/AhoiDev` were not changed.
- Verification: in series order every patch applies with
  `git apply --cached --whitespace=error-all` on the sparse M154 base (result
  tree identical to the rebase branch), and through `tools/compose_overlay.py
  --base-revision f89f3a43…` on the full M154 index with the overlay staged.
  Compose stops only at the final `write-tree`, which needs the full M154
  blob closure (`fetch-chromium.sh --prehydrate-target` at the roll).

## Dispositions

- **Dropped: `0018-ahoi-arc-import-web-component-build.patch`.** M154 builds
  the people-page dialogs as Lit `ts_files`; 0013 now puts
  `ahoi_arc_import_section.ts` there directly, which was 0018's only change.
  The series has 83 entries.
- **Rebased with content changes (19):**
  - `0001` M154 finished part of the `Browser` → `BrowserWindowInterface`
    migration and removed `GetBrowserForMigrationOnly()`: live-tab restore,
    popup close, uBO action, Quick Window lookup
    (`FindLastRegularAhoiBrowser`/`ShowAhoiQuickWindow` now take
    `BrowserWindowInterface*`), omnibox click, startup browser tests.
    `kCreateBrowserOnStartupForTests` moved to `ash::switches`;
    `GetLinuxStartupId` also takes the XDG activation token like M154.
    Saved-tab-group bar: M154 passes `visible`; Ahoi masks it on Ahoi
    surfaces. `kGlassFrame` params shrank to `kGlassExpandOnHoverEnabled`.
    The glass widget's `background_view_` is now `glass_background_view_`;
    `last_preferred_color_scheme_` is gone (tint darkness from the theme
    colour). `kVerticalTabsEnabled` default `true` kept on the new
    registration; M154's removed `FeatureDisabled` test is not revived.
    Login handler keeps M154's `PasswordString` conversion.
  - `0004` compose guard next to M154's new `//chrome/browser/indigo` dep.
  - `0013`, `0015`, `0025` import dialog template moved from
    `import_data_dialog.html` to Lit `import_data_dialog.html.ts`
    (same content change).
  - `0014` uBO action calls `chrome::ExecuteCommand(bwi, …)`.
  - `0045` full-window glass uses `glass_background_view_` and hides M154's
    new opaque background view, which would otherwise sit below the milky
    glass and block the desktop.
  - `0050` M154 no longer preselects a `SiteInstance` for restored
    default-partition tabs; only a fixed website-session partition creates
    one.
  - `0067` M154 added its own per-renderer GPC setting
    (`is_global_privacy_control_setting_enabled`,
    `IsGlobalPrivacyControlFeatureAndSettingEnabled`, gated by
    `kGlobalPrivacyControlTest`). Ahoi's `enable_global_privacy_control`
    stays as an OR, so behaviour is unchanged. Convergence candidate: set
    the upstream field and drop most of 0067 after the roll.
  - `0071`, `0073` regenerated with `tools/branding/make_rebrand_patch.sh`
    on the rebased tree (GRIT from the checkout; its Python is identical in
    M153 and M154); M154 has one more password-manager message. `0074`
    regenerated identically.
  - `0080` M154 paints the window area outside the glass with an opaque
    background view and `BrowserFrameViewMac` no longer paints there in glass
    mode. The frame view now returns early only when Ahoi glass is active
    and paints the semantic chrome surface otherwise; the widget colours the
    opaque view with that surface instead of the saturated frame colour.
    `GetGlassFrameTintOpacity` stays removed.
  - `0044`, `0047`, `0052`, `0053`, `0054`, `0058`, `0063` only hunk order
    changed.
- **Context-only (the other 64):** same added/removed lines, new context
  and blob ids.

## Overlay compatibility with M154 (not changed here)

The overlay is shared with the active M153 build and stays untouched.

- Known break: `ahoi/browser/command_bar/quick_window_chromium.cc` calls
  `GetBrowserForMigrationOnly()` three times; M154's
  `BrowserWindowInterface` no longer has it.
- `features::IsGlassFrameEnabled()` and `kGlassFrame` still exist.
- Of 659 Chromium headers the overlay includes, none was removed; these 105
  changed between M153 and M154 and are the first places to look when the
  M154 build fails:

```text
base/base_switches.h
base/containers/flat_map.h
base/feature_list.h
base/i18n/rtl.h
base/i18n/time_formatting.h
base/rand_util.h
base/strings/string_util.h
base/supports_user_data.h
base/synchronization/lock.h
base/system/sys_info.h
base/threading/thread_restrictions.h
chrome/app/chrome_command_ids.h
chrome/browser/browser_process.h
chrome/browser/glic/test_support/glic_test_environment.h
chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h
chrome/browser/profiles/profile_attributes_storage.h
chrome/browser/profiles/profile_manager.h
chrome/browser/profiles/profile_metrics.h
chrome/browser/profiles/profile_selections.h
chrome/browser/profiles/profile.h
chrome/browser/themes/theme_service.h
chrome/browser/ui/browser_commands.h
chrome/browser/ui/browser_element_identifiers.h
chrome/browser/ui/browser_live_tab_context.h
chrome/browser/ui/browser_tabstrip.h
chrome/browser/ui/browser_window.h
chrome/browser/ui/browser_window/public/browser_window_features.h
chrome/browser/ui/browser_window/public/browser_window_interface.h
chrome/browser/ui/browser.h
chrome/browser/ui/color/chrome_color_id.h
chrome/browser/ui/login/login_tab_helper.h
chrome/browser/ui/tabs/features.h
chrome/browser/ui/tabs/tab_enums.h
chrome/browser/ui/tabs/tab_strip_model.h
chrome/browser/ui/thumbnails/thumbnail_tab_helper.h
chrome/browser/ui/views/frame/browser_view.h
chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h
chrome/browser/ui/views/location_bar/location_bar_view.h
chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_bottom_container.h
chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h
chrome/browser/ui/views/toolbar/toolbar_view.h
chrome/common/chrome_features.h
chrome/common/pref_names.h
chrome/common/webui_url_constants.h
chrome/test/base/browser_with_test_window_test.h
chrome/test/base/in_process_browser_test.h
chrome/test/base/test_browser_window.h
chrome/test/base/testing_browser_process.h
chrome/test/base/ui_test_utils.h
chrome/test/interaction/interactive_browser_test.h
components/embedder_support/user_agent_utils.h
components/history/core/browser/history_types.h
components/javascript_dialogs/app_modal_dialog_controller.h
components/password_manager/core/browser/password_form.h
components/performance_manager/public/user_tuning/prefs.h
components/permissions/permission_request_manager.h
components/search_engines/default_search_manager.h
components/search_engines/template_url_prepopulate_data.h
components/search_engines/template_url_service.h
components/search_engines/template_url.h
components/signin/public/base/signin_pref_names.h
components/tabs/public/tab_alert.h
content/public/browser/browsing_data_remover.h
content/public/browser/devtools_agent_host_client.h
content/public/browser/navigation_handle.h
content/public/browser/render_frame_host.h
content/public/browser/storage_partition.h
content/public/browser/web_contents_delegate.h
content/public/browser/web_contents.h
content/public/test/mock_navigation_handle.h
content/public/test/navigation_simulator.h
content/test/test_web_contents.h
crypto/aead.h
crypto/hash.h
crypto/keypair.h
crypto/sign.h
extensions/common/extension.h
extensions/common/permissions/permissions_data.h
mojo/core/embedder/embedder.h
net/base/registry_controlled_domains/registry_controlled_domain.h
net/dns/mock_host_resolver.h
net/http/http_request_headers.h
services/network/public/cpp/simple_url_loader.h
sql/database.h
third_party/blink/public/common/renderer_preferences/renderer_preferences.h
ui/base/l10n/l10n_util.h
ui/base/ui_base_features.h
ui/color/color_id.h
ui/color/color_provider_key.h
ui/compositor/layer.h
ui/events/event_constants.h
ui/events/gesture_event_details.h
ui/native_theme/native_theme.h
ui/views/accessibility/view_accessibility.h
ui/views/background.h
ui/views/bubble/bubble_dialog_delegate_view.h
ui/views/controls/textfield/textfield.h
ui/views/controls/webview/webview.h
ui/views/focus/focus_manager.h
ui/views/layout/fill_layout.h
ui/views/view_shadow.h
ui/views/view.h
ui/views/widget/widget_delegate.h
ui/views/widget/widget.h
ui/views/window/dialog_delegate.h
```

## Before the first M154 build

1. Roll commit: `config/chromium.json` ← candidate; this stack replaces
   `patches/chromium/`.
2. `config/dependency-build-workarounds.json`: both target files are
   byte-identical on M154 (`rustc_wrapper.py`, V8
   `code_generator.py` at V8 `31fac3bef58c3def36b0760e4ddc54ec77099596`);
   only `upstreamCommit` needs repinning.
3. Fix the overlay's Quick Window migration call sites.
4. `fetch-chromium.sh --prehydrate-target`, dependency sync, hooks, then the
   upstream control build and the Ahoi build.
