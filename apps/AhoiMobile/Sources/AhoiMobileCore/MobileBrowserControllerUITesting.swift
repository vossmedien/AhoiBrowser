import Foundation
import WebKit

extension MobileBrowserController {
#if DEBUG
    public func loadUITestFixture() {
        uiTestRetryResponses.removeAll()
        dialogPresenters.values.forEach { $0.cancelPending() }
        dialogPresenters.removeAll()
        linkInteractionCoordinators.values.forEach { $0.invalidate() }
        linkInteractionCoordinators.removeAll()
        navigationObservationTasks.values.forEach { $0.cancel() }
        navigationObservationTasks.removeAll()
        navigationDocumentGenerations.removeAll()
        pages.removeAll()
        websiteDataStores.removeAll()
        privateWebsiteDataStore = nil
        tabs.removeAll()
        selectedTabID = nil
        recentlyClosedTab = nil
        let fixtureURL = URL(string: "https://fixture.ahoibrowser.test/start")!
        let tabID = createTab()
        guard let page = page(for: tabID, createIfBlank: true) else { return }
        page.load(
            simulatedRequest: URLRequest(url: fixtureURL),
            responseHTML: """
            <!doctype html><html lang="en"><head>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <meta name="theme-color" content="#f97316">
            <title>Ahoi Fixture</title>
            <style>
              :root { accent-color:#f97316; color-scheme:light dark }
              body { font:17px -apple-system,sans-serif; margin:28px; line-height:1.45 }
              h1 { color:#c2410c }
              button { background:#f97316; color:white; border:0; border-radius:10px; padding:12px }
              .fixture-actions { display:grid; gap:10px; max-width:340px; margin:20px 0 }
              .fixture-actions,input { max-width:100%; box-sizing:border-box }
              input[type="file"] { width:100% !important }
              label { display:block; font-weight:600; margin:18px 0 8px }
            </style></head><body>
            <main><h1>Ahoi fixture page</h1>
            <p>This page is provided locally for deterministic browser UI tests.</p>
            <p id="find-target">Ahoi visible find target</p>
            <ul>
              <li><a href="https://example.com">Open HTTPS page</a></li>
              <li><a id="link-actions-fixture" href="https://example.com/ahoi-link-actions" aria-label="Open Ahoi link actions">Long-press for link actions</a></li>
              <li><a href="https://example.com/?ahoi-popup=1" target="_blank">Open target blank</a></li>
              <li><a href="https://httpbin.org/response-headers?Content-Disposition=attachment%3B%20filename%3Dahoi-fixture.txt&amp;Content-Type=text%2Fplain">Download fixture</a></li>
              <li><a href="mailto:browser-test@example.com">Open mail app</a></li>
            </ul>
            <section class="fixture-actions" aria-label="JavaScript dialog fixtures">
              <button id="js-alert" aria-label="Show JavaScript alert" onclick="alert('Ahoi alert fixture')">Show JavaScript alert</button>
              <button id="js-confirm" aria-label="Show JavaScript confirm" onclick="showFixtureConfirm()">Show JavaScript confirm</button>
              <button id="js-prompt" aria-label="Show JavaScript prompt" onclick="showFixturePrompt()">Show JavaScript prompt</button>
              <output id="dialog-result" aria-live="polite">No dialog result yet.</output>
            </section>
            <label for="upload">Choose a fixture file</label>
            <p><input id="upload" aria-label="Choose a fixture file" type="file" style="font-size:20px;padding:12px;border:1px solid #777;border-radius:10px;width:340px"></p>
            <section class="fixture-actions" aria-label="Website permission fixtures">
              <button id="camera" onclick="requestMedia('camera')">Request camera</button>
              <button id="microphone" onclick="requestMedia('microphone')">Request microphone</button>
              <button id="motion" onclick="requestMotion()">Request motion</button>
              <output id="permission-result" aria-live="polite">No permission result yet.</output>
            </section>
            </main>
            <script>
              const dialogResult = document.getElementById('dialog-result');
              function showFixtureConfirm() {
                dialogResult.textContent = confirm('Confirm the Ahoi fixture?')
                  ? 'Confirm accepted.'
                  : 'Confirm cancelled.';
              }
              function showFixturePrompt() {
                const value = prompt('Enter the Ahoi fixture value', 'Ahoi');
                dialogResult.textContent = value === null
                  ? 'Prompt cancelled.'
                  : 'Prompt value: ' + value;
              }
              const permissionResult = document.getElementById('permission-result');
              async function requestMedia(kind) {
                try {
                  const constraints = kind === 'camera' ? {video:true} : {audio:true};
                  const stream = await navigator.mediaDevices.getUserMedia(constraints);
                  permissionResult.textContent = kind + ' granted.';
                  stream.getTracks().forEach(track => track.stop());
                } catch (error) {
                  permissionResult.textContent = kind + ' denied: ' + (error?.name || 'Error') + '.';
                }
              }
              async function requestMotion() {
                try {
                  const result = typeof DeviceMotionEvent.requestPermission === 'function'
                    ? await DeviceMotionEvent.requestPermission()
                    : 'available';
                  permissionResult.textContent = 'motion ' + result + '.';
                } catch (error) {
                  permissionResult.textContent = 'motion denied: ' + (error?.name || 'Error') + '.';
                }
              }
            </script></body></html>
            """
        )
        updateSelectedMetadata(url: fixtureURL, title: "Ahoi Fixture")
    }

    public func loadUITestOfflineFailure() {
        uiTestRetryResponses.removeAll()
        dialogPresenters.values.forEach { $0.cancelPending() }
        dialogPresenters.removeAll()
        linkInteractionCoordinators.values.forEach { $0.invalidate() }
        linkInteractionCoordinators.removeAll()
        navigationObservationTasks.values.forEach { $0.cancel() }
        navigationObservationTasks.removeAll()
        navigationDocumentGenerations.removeAll()
        pages.removeAll()
        websiteDataStores.removeAll()
        privateWebsiteDataStore = nil
        tabs.removeAll()
        selectedTabID = nil
        pageFailures.removeAll()
        let fixtureURL = URL(string: "https://fixture.ahoibrowser.test/offline")!
        let tabID = createTab()
        guard page(for: tabID, createIfBlank: true) != nil else { return }
        updateSelectedMetadata(url: fixtureURL, title: "Ahoi Offline Fixture")
        uiTestRetryResponses[tabID] = (
            request: URLRequest(url: fixtureURL),
            html: """
            <!doctype html><html lang="en"><head>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <meta name="theme-color" content="#0f766e">
            <title>Ahoi Retry Fixture</title>
            <style>
              :root { color-scheme:light dark }
              body { font:17px -apple-system,sans-serif; margin:28px; line-height:1.45 }
              h1 { color:#0f766e }
            </style></head><body>
            <main aria-live="polite">
              <h1 id="retry-recovered">Ahoi is back online</h1>
              <p>The deterministic retry completed without network access.</p>
            </main></body></html>
            """
        )
        pageFailures[tabID] = .offline
    }
#endif
}
