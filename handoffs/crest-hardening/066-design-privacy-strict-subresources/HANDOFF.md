# 066 – Design: "Mehr Schutz" for subresources, workers and the JS signal

Status: ready (design; implementation by desktop, or by this lane as a patch
handoff on request)
Owner lane: desktop
Base: HEAD `57a0949`. Owner's DoD-10 finding: strict mode reaches navigations
only.

## Facts

- `PrivacyModeURLLoaderThrottle` (`ahoi/browser/privacy/privacy_mode_url_loader_throttle.cc`)
  sets `Sec-GPC: 1`, reduces cross-site referrers to the origin, removes
  high-entropy UA client hints for third parties and strips tracking
  parameters (main frame only). It is registered only in
  `ChromeContentBrowserClient::CreateURLLoaderThrottles` (0001), which
  upstream calls for browser-side loaders (navigation, worker scripts,
  service-worker scripts, prefetch, early hints, keep-alive). Renderer
  subresources (img, script, fetch, XHR) never pass it.
- Build 35 strict fixture: `/top` has `gpc=1`; `/frame` (subframe
  navigation) has `gpc=1` and a reduced referrer; `/pixel` (img) has no
  GPC and the full referrer `…/top`, so PRIV-05 fails.
- Upstream GPC is a **process-global** Blink feature
  (`IsGlobalPrivacyControlEnabled()`: `kGlobalPrivacyControlForce/Test`,
  with a TODO for a pref). Where the flag is on, the renderer adds the header in
  `RenderFrameImpl::FinalizeRequestInternal` (next to the per-profile DNT from
  `RendererPreferences.enable_do_not_track`), plus the browser-initiated and
  worker equivalents. Ahoi does not expose `navigator.globalPrivacyControl`
  at all, although GPC means header **and** JS property.
- Contract: Master 1488–1507 (GPC active, conservative tracking parameters,
  cross-site referrers reduced, fingerprinting protection in third-party
  contexts); `docs/PRIVACY.md:12-21` (client hints removed from third-party
  **subresources**); PRIV-04/05, PRIV-17 (default unchanged).

## Decision (proposal)

One authoritative browser-side mechanism for the request rules, and one
renderer preference for the JS signal. Parallel stores, and a second
decision point in the renderer for referrers, are excluded.

1. **Request rules for every request of a strict document: URLLoaderFactory
   proxy.** Use `ContentBrowserClient::WillCreateURLLoaderFactory` (in the
   same 0001 seam as the throttle) for frames (`kDocumentSubResource`,
   `kNavigation` is already covered), dedicated/shared workers and service
   workers. When the factory is created, resolve the mode for the
   document's **top-level site** (the same `PrivacyPolicy::ModeForUrl` and
   the same origin exceptions as the throttle). Only for `strict` interpose
   `PrivacyModeURLLoaderFactoryProxy`, which calls one shared function
   `ApplyStrictRequestRules(ResourceRequest&, top_level_site)` (extracted
   from `ApplyToRequest`, so throttle and proxy cannot diverge):
   - `Sec-GPC: 1`;
   - a cross-site `request.referrer` reduced to its origin and
     `referrer_policy = ORIGIN`, regardless of the page's own policy (the
     fixture sets `unsafe-url`);
   - high-entropy UA-CH removed when the request is third-party relative to
     the top-level site;
   - redirects handled through the same function (the throttle's redirect
     path does not reduce referrers today, a gap to close at the same time);
   - tracking parameters stay main-frame only, as the contract says
     "konservativ".
   Default mode installs no proxy (PRIV-17: no rewriting, no extra hop).
   Factories are per committed document, so a mode change applies from the
   next load of the page. The site control's text states this.
2. **JS signal:** an upstream patch mirroring DNT:
   `blink::RendererPreferences.enable_global_privacy_control` (mojom and
   struct traits). `RenderFrameImpl::FinalizeRequestInternal` and the
   worker fetch contexts send `Sec-GPC` when it is set (renderer-side
   defence in depth, identical to the proxy). The Blink getter
   `navigator.globalPrivacyControl` returns it (enable the IDL attribute
   unconditionally; the value follows the preference). Ahoi sets the
   preference per WebContents from the mode of the committed primary main
   frame (`DidFinishNavigation` → `SyncRendererPrefs`), and for workers
   from their creating document.
3. **Not chosen:** a renderer `URLLoaderThrottleProvider` for everything. It
   would need the per-origin mode in the renderer, plus a second decision
   point for referrer and UA-CH; the proxy keeps the browser authoritative.

## Cost and evidence

- The proxy adds one browser-process hop per subresource, only in strict
  mode. Measure it under `docs/PERFORMANCE_METHODOLOGY.md`: page-load
  scenario, strict vs default, same candidate; no budget claim without it.
- Tests:
  - unit tests for `ApplyStrictRequestRules` (same site, cross-site,
    third-party UA-CH, redirect) and for proxy installation (strict yes,
    default no, origin exception);
  - fixture extensions: `/pixel` expects `gpc=1` and `referer=<A>/`;
    a third-party image after `Accept-CH` expects no high-entropy hints;
    `fetch()` from a worker expects `gpc=1`; `navigator.globalPrivacyControl`
    is `true` in strict and `false` in default; the default run must stay
    unchanged (PRIV-17).

## Journey note

The build-35 `results.txt` (02:46) predates the journey fixes `fe7dd08` and
`1c6e2ad`. Its PRIV-17 and tracking-parameter failures are partly journey
artefacts; rerun before judging them.
