# 090 – Mobile: WebKit Web Extension spike (ADR 0012 section 4, step 1, MOB-EXT-01)

Status: ready
Owner lane: mobile (apply, regenerate the project, build, run the spike checklist)
Base: HEAD `2543785` (`git apply --check` passes).
Plan of record: `outputs/AhoiBrowser-Mobile-uBlock-Feasibility.md`
(`MobileWebExtensionRuntime`; the `MobileWebExtensionHost` tab/window adapters
are **not** part of this step).

## What the iOS SDK offers (Xcode 27, iPhoneSimulator 27.0 SDK)

- `WebPage.Configuration.webExtensionController: WKWebExtensionController?`
  is public in the SwiftUI WebKit interface. No `WKWebView` bridge is needed to
  attach a controller to Ahoi's `WebPage`.
- `WKWebExtension(resourceBaseURL:) async throws` (and `init(appExtensionBundle:)`);
  `displayName`, `manifestVersion`, `requestedPermissions`,
  `allRequestedMatchPatterns`, `hasInjectedContent`,
  `hasContentModificationRules`, `errors`.
- `WKWebExtensionContext(for:)` with `setPermissionStatus(_:for:)` for a
  `WKWebExtension.Permission`, a URL or a `WKWebExtensionMatchPattern`
  (`.grantedExplicitly`, `.deniedExplicitly`, …).
- `WKWebExtensionController(configuration: .nonPersistent())`, `load(_:)`,
  `unload(_:)`, `extensionContexts`. Also `WebKit.WKWebViewConfiguration.webExtensionController`
  (iOS 18.4+), which isn't needed here.
- Missing on the `WebPage` path: `WebPage` is not a `WKWebExtensionTab` and
  doesn't hand out its `WKWebView`. So `WKWebExtensionTab.webView(for:)`, tab
  and window events, `activeTab`, the action popup's tab context and
  `tabs.*` APIs can't be served without a tab adapter or a switch to
  `WKWebView`. The checklist below measures what works without that.

## Change (`090-mobile-web-extension-spike.patch`)

- New `MobileWebExtensionRuntime` (`@MainActor`):
  - It is **off** unless the app is launched with `-AhoiWebExtensionSpike`.
  - It owns one non-persistent `WKWebExtensionController`.
  - It loads the bundled test extension asynchronously and grants the
    manifest's permissions and patterns. That is debug-only; MOB-EXT-03
    replaces it with a prompt.
  - It exposes `state` (`disabled`, `loading`, `loaded`, `failed`).
- `makePage` calls `attach(to:mode:usesDefaultWebsiteDataStore:)` before
  `WebPage(configuration:)`. It attaches only for `.normal` pages on the
  default data store: never for private pages or separated Workspaces.
- Test extension `Sources/AhoiMobileCore/WebExtensionSpike/`:
  - MV3 manifest ("Ahoi Spike"; permissions `declarativeNetRequest`, `storage`;
    host `<all_urls>`).
  - `content.js` sets `data-ahoi-spike="1"` on `<html>` and counts visits in
    `storage.local` (`data-ahoi-spike-visits`).
  - A DNR rule blocks scripts whose URL contains `/ahoi-spike-blocked.js`.
  - `popup.html` shows the visit count.
- Resources:
  - `Package.swift`: `.copy("WebExtensionSpike")`.
  - `project.yml`: the folder is excluded from the core sources and added as a
    `type: folder` resource.
  - **Run `xcodegen generate`** so `AhoiMobile.xcodeproj` picks up the new
    Swift file and the folder. The patch does not edit `project.pbxproj`.
- New `MobileWebExtensionRuntimeTests` (no UI):
  - The bundled extension loads with name, MV3, permissions, injected content
    and DNR rules, and without errors.
  - The runtime is off without the argument.
  - It attaches only to normal default-store pages, and after loading the
    controller holds one context.

Checked by this lane: `AhoiMobileCore` with the patch and the new test file
typecheck (`swiftc -emit-module -enable-testing`, `-typecheck`, iOS 26 simulator
target, iPhoneSimulator 27.0 SDK, `-D DEBUG`). Not built or run. The resource
lookup (`Bundle(for:)` or `Bundle.module`) is only proven by running the test.

## Spike checklist (owner, simulator build with `-AhoiWebExtensionSpike`)

| Item | Pass criterion |
| --- | --- |
| Load | `MobileWebExtensionRuntimeTests` 3/3 green; `state == .loaded("Ahoi Spike")` in the app. |
| Content script | On a local fixture page, after a reload following the load, `document.documentElement.dataset.ahoiSpike == "1"` (read it via the page's JavaScript bridge or a visible fixture that prints it). |
| DNR | A fixture page that loads `/ahoi-spike-blocked.js` (it would set a marker) and `/ok.js`: only `ok.js` runs, and the server log shows no request for the blocked file. |
| Storage | `data-ahoi-spike-visits` counts up over three navigations; after an app relaunch it starts again at 1 (non-persistent controller, as intended). |
| Action/popup | Record whether the popup can be shown at all without a `WKWebExtensionTab`/window adapter (`WKWebExtensionAction`, `presentActionPopup` delegate). Expected: not without `MobileWebExtensionHost`, which is a documented gap, not a failure of this step. |
| Permission prompts | With the grant loop removed (local edit), check that the content script does **not** run and that `WKWebExtensionControllerDelegate` permission callbacks need the host adapter. Note the exact callback that fires. |
| Private and separated | No marker in a private tab or in a `Vollständig getrennt`/separated Workspace. |
| App Review 2.5.2 | Record the argument: the extension is bundled, reviewed with the app and never downloaded, with no remote code (feasibility doc, update section). If extensions installed from Files are planned (MOB-EXT-02), write down the review risk for loading user-supplied code. |
| Privacy manifest | `PrivacyInfo.xcprivacy` needs no new required-reason API for the spike. Confirm with Xcode's privacy report on the archive. |
| Update path | The extension changes only with an app update. Loading a changed bundle folder after an app update replaces the context (unload/load), with non-persistent storage. |

The result goes into the mobile checkpoint as evidence. The user decides on
step 2 (MOB-EXT-02/03) from it. Keep the launch argument off in release builds.
