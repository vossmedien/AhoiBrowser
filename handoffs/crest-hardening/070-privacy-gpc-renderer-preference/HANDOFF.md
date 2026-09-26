# 070 – navigator.globalPrivacyControl and renderer Sec-GPC per top-level site (066 part 2)

Status: integrated 79d6851 (reviewed: bindings expose on RuntimeEnabled OR ContextEnabled, so the getter works with the runtime flag off; patches 0067/0068; overlay syntax-checked; build blocked by free disk space until the owner frees space)
Owner lane: desktop (apply overlay patch, add two Chromium patches to the series, build, test)
Base: HEAD `59d7807` (the overlay patch passes `git apply --check`; it needs 068's
`privacy_strict_request_rules` from `fca2c38`). The Chromium patches are in
upstream-tree form against the current `.work` series. Syntax-checked
read-only against `out/AhoiDev`: every touched `.cc` has 0 errors, except
`renderer_preferences_mojom_traits.cc`, which needs the regenerated mojom
(new DataView accessor) and is only compiled by the build.

## Chromium patch A: `chromium-renderer-preferences-gpc.patch` (17 files)

Mirrors the per-profile Do-Not-Track preference that already sits next to
every upstream GPC call site:

- `blink::RendererPreferences::enable_global_privacy_control` (struct, mojom,
  traits).
- Sec-GPC is sent when `IsGlobalPrivacyControlEnabled() ||
  enable_global_privacy_control` in:
  - `RenderFrameImpl::FinalizeRequestInternal`;
  - the dedicated/shared worker context;
  - `WebServiceWorkerFetchContextImpl`;
  - `browser_initiated_resource_request.cc` (navigations and worker
    scripts).
  This is defence in depth: 068's proxy stays authoritative.
- `navigator.globalPrivacyControl`: the IDL mixin gets
  `ContextEnabled=GlobalPrivacyControlPreference` next to the existing
  `RuntimeEnabled` (OR semantics), and the getter also returns true for that
  context feature.
  - **Window:** `RenderFrameImpl::DidCreateScriptContext` calls the new
    `WebV8Features::EnableGlobalPrivacyControl` for the main world when the
    preference is set. That is the MojoJS pattern: `LocalWindowProxy` calls
    `DidCreateScriptContext` before `InstallConditionalFeatures` (local_window_proxy.cc 307/310).
  - **Workers:** `WorkerOrWorkletGlobalScope`'s constructor enables it from
    the new `WebWorkerFetchContext::IsGlobalPrivacyControlEnabledForContext()`,
    implemented by both worker fetch contexts. Conditional features are
    installed in `PrepareForEvaluation` before the first script.
  - **Default mode:** nothing is installed, so `navigator.globalPrivacyControl`
    is `undefined` as in Chromium (PRIV-17), not `false`.

## Chromium patch B: `chromium-tab-features-gpc-seam.patch`

`TabFeatures` owns `ahoi::privacy::GpcRendererPreferenceTabHelper` next to
0001's `DeveloperProfileTabHelper` (Init; `SetWebContents` on discard);
`chrome/browser/ui/tabs` gets `//ahoi/browser/privacy:gpc_renderer_preference`.

## Overlay: `overlay-gpc-tab-helper.patch`

- `GlobalPrivacyControlForMainFrameUrl(prefs, otr, url)` in
  `privacy_strict_request_rules`: HTTP(S) and a strict top-level site. Off the
  record, exceptions are not consulted (`GetPolicySnapshot`).
- `GpcRendererPreferenceTabHelper` (new target `gpc_renderer_preference`)
  sets `enable_global_privacy_control` and `SyncRendererPrefs()` at
  `DidStartNavigation` and `DidRedirectNavigation` of the primary main
  frame (not same-document) and at construction or discard, only when the
  value changes.
- Test: `GpcRendererPreferenceTest.FollowsTopLevelSiteMode`.

## Timing (stated)

The value is set when the navigation starts. A cross-process navigation
creates the new view afterwards and carries the prefs in its creation
parameters. In the same process, the prefs update travels on the page
broadcast channel long before the response commits, but mojo does not order
it against `CommitNavigation`. A document committed faster than the
update would start with the old value and get the right one on its next
load. The header is not affected (068 proxy). The journey should check
the getter on the first load of a strict site after a default page.

## Scope limits

- Only tab WebContents (TabFeatures). Other WebContents (DevTools,
  extension pages) keep the upstream default; the proxy still covers their
  requests.
- Cross-site iframes follow the top-level site, like the proxy.

## Tests

- Unit: `GpcRendererPreferenceTest.FollowsTopLevelSiteMode`; the existing
  renderer-preferences mojom traits round-trip test (if run) covers the new
  field.
- Journey (`b89ef23` fixture): strict `/top` `navigator.globalPrivacyControl
  === true`; a dedicated worker's `navigator.globalPrivacyControl === true`;
  default `typeof navigator.globalPrivacyControl === "undefined"`; after
  switching a site to compatibility and reloading, `undefined`.
