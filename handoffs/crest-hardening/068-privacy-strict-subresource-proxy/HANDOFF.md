# 068 – Strict privacy for subresources: shared rules + factory proxy (066 part 1)

Status: ready
Owner lane: desktop (apply overlay patch, add the Chromium patch to the series, build, test)
Base: HEAD `4d108a6` (the overlay patch passes `git apply --check`). The Chromium
patch is in upstream-tree form against `chrome_content_browser_client.cc`
with the current series applied (it touches the include next to 0001's
`privacy_mode_url_loader_throttle.h`). Every file is syntax-checked read-only
against `out/AhoiDev` (0 errors, style plugin included); not built.

## Contents

1. `privacy_strict_request_rules.{h,cc}`: `ApplyStrictRequestRules(request,
   is_main_frame)` sets `Sec-GPC: 1`, removes high-entropy UA-CH from
   third-party requests, and reduces a cross-origin referrer to its origin
   (policy `ORIGIN`). Otherwise it caps the page's policy with
   `CapReferrerPolicyForStrictMode` (`unsafe-url`,
   `no-referrer-when-downgrade` and `origin-when-cross-origin` become
   `strict-origin-when-cross-origin`; stricter policies stay).
2. `PrivacyModeURLLoaderThrottle` uses it. The main-frame tracking-parameter
   strip is unchanged. **Redirect gap closed:** the network service applies the
   capped policy on every hop, so a request that starts same-origin and
   redirects cross-site sends only the origin. The navigation throttle had
   left the full referrer there.
3. `privacy_mode_url_loader_factory_proxy.{h,cc}`: a `SelfDeletingURLLoaderFactory`
   modelled on `HttpHeaderInjectionProxyingURLLoaderFactory`. It copies each
   request, applies the rules (not main frame) and forwards it. It never
   blocks or completes a request itself.
   `MaybeProxyPrivacyModeURLLoaderFactory(prefs, otr, is_subresource_factory,
   isolation_info, initiator, builder)` appends it only when the factory
   serves subresources and the policy site is strict.
4. Chromium seam, in the first lines of
   `ChromeContentBrowserClient::WillCreateURLLoaderFactory` (as a new patch
   after 0065, not an edit of 0001): for `kDocumentSubResource`,
   `kWorkerSubResource` and `kServiceWorkerSubResource`, before the
   webRequest proxy.
5. BUILD: new sources in `privacy_mode_runtime` (+ `//mojo/public/cpp/bindings`),
   and two new test files in `unit_tests`.

## The owner's four points

- **Extension proxies (uBO Classic, webRequest):** `URLLoaderFactoryBuilder`
  processes interceptors in `Append()` order, and the seam appends the
  privacy proxy **first**. webRequest therefore sees the request already with
  `Sec-GPC` and the reduced referrer, and can modify or block it; the privacy
  proxy forwards every request and never completes one itself. Unit tests
  `WebRequestSeesStrictRequest` and `WebRequestCanStillBlock` (a later
  interceptor completes with `ERR_BLOCKED_BY_CLIENT`; the network sees
  nothing). The enterprise header client wrapper stays last, as upstream
  requires.
- **Workspace website sessions:** `WillCreateURLLoaderFactory` is called for
  the factories of every StoragePartition of the profile, the non-default
  ones of 4a11c52/060 included. The seam does not look at the partition; the
  mode follows the top-level site alone. It uses the same profile prefs and
  the same exceptions as the throttle.
- **Service workers (decision, conservative):** the policy site is
  `isolation_info.top_frame_origin()`, the same rule for frames, workers
  and service workers. A storage-partitioned service worker therefore follows
  the top-level site it was registered under. An unpartitioned one follows its
  own origin (fallback `request_initiator`), i.e. the site it runs for, not
  the page it happens to control. A strict site's worker never loses the
  rules because a compatibility page uses it. A compatibility site's worker
  serving a strict page is not rewritten, but the page's own requests are.
  This is documented in `PolicySiteForSubresourceFactory`.
- **Off the record:** `GetPolicySnapshot(prefs, is_off_the_record)` ignores
  per-site exceptions off the record, as in PRIVACY_POLICY.md. The global mode
  applies as in the throttle.

## Stated limits

- The mode is snapshotted when the factory is created, i.e. per committed
  document. A mode change applies from the next load, and the site control
  says so. This is the same rule as factories generally.
- High-entropy UA-CH that the renderer adds on a **redirect** of a
  subresource (`FollowRedirect` modified headers) bypass the factory proxy.
  Closing that needs a URLLoader proxy. It is not in this patch, because the
  initial third-party request, the case PRIVACY.md names, is covered.
- The JS signal `navigator.globalPrivacyControl` is part 2 (upstream
  `RendererPreferences.enable_global_privacy_control` patch), next handoff.
- Cost: one browser-process hop per subresource in strict mode only. Measure
  under `docs/PERFORMANCE_METHODOLOGY.md` (page load, strict vs default, same
  candidate) before any budget claim.

## Tests

- `PrivacyStrictRequestRulesTest` (3), `PrivacyModeURLLoaderFactoryProxyTest`
  (4: webRequest sees it, can block, default untouched, non-subresource
  factory not proxied), `PrivacyModePolicySiteTest.TopFrameOriginDecides`.
  The existing `PrivacyModeURLLoaderThrottleTest` needs no change (none of it
  asserts an unchanged same-origin policy).
- The privacy journey on the next build: strict `/pixel` has `gpc=1` and
  `referer=<A>/`; default unchanged (PRIV-17). Suggested fixture additions
  are in 066.
