# webRequest subresource P0 — RED on e86c929f, fixed in b21aa146 — 5 October 2026

**RED (installed e86c929f, MacbookPro2026.local):** with uBlock Origin Classic
(ubo-classic journey, 11:32) and with AnyChat (anychat journey, 11:38)
installed, the browser aborted on a document subresource request:
`DCHECK_EQ(is_navigation_request, navigation_id.has_value())` in
`extensions::WebRequestInfoInitParams` (`web_request_info.cc:272`) via
`WebRequestProxyingURLLoaderFactory::InProgressRequest::UpdateRequestInfo`.
Crash summaries: `e86-red/`.

**Cause:** patch 0094 binds document subresource factories to their
navigation ID for the Ahoi developer/privacy adapters; the same ID reached
`WebRequestAPI::MaybeProxyURLLoaderFactory`, which reads any ID as a
navigation request.

**Fix:** `b21aa146`, patch 0089 — webRequest receives the ID only for
`URLLoaderFactoryType::kNavigation`, as upstream does. Incremental build
(35 steps), Apple Development signing in the GUI session, provenance and
canonical installation: `build-provenance.json`, `installed.json`
(source b21aa146, binary sha256 1f672505…).

**GREEN (installed b21aa146):** `tools/desktop_e2e/webrequest-subresource-probe.sh`
— minimal MV3 webRequest extension in a fresh profile, page with stylesheet,
script and images, fresh navigation and reload: browser alive after load,
navigate and reload; the extension observed main_frame, stylesheet, script
and image requests (they ran through the webRequest proxy); no
`web_request_info.cc` DCHECK. Runs without input events (console was locked).

**Open:** the full visible ubo-classic journey (filtering, incognito,
restart) on b21aa146 waits for an unlocked target console; its first run
ended at setup because loginwindow held the foreground.
