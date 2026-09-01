import Foundation
import OSLog
import UIKit
import WebKit

extension MobileBrowserController {
#if DEBUG
    public func loadUITestFixture() {
        let arguments = ProcessInfo.processInfo.arguments
        loadLocalFixture(
            normalTabCount: uiTestTabCount(
                after: "-AhoiUITestNormalTabCount",
                in: arguments,
                defaultValue: 1
            ),
            privateTabCount: uiTestTabCount(
                after: "-AhoiUITestPrivateTabCount",
                in: arguments,
                defaultValue: 0
            ),
            selectPrivate: arguments.contains("-AhoiUITestSelectPrivate")
        )
    }

    public func loadPerformanceFixture(_ request: MobilePerformanceLaunchRequest) {
        loadLocalFixture(
            normalTabCount: request.normalTabCount,
            privateTabCount: request.privateTabCount,
            selectPrivate: request.selectPrivate
        )
        performanceRecorder.completeLaunch(
            normalTabs: normalTabs.count,
            privateTabs: privateTabs.count,
            livePages: pages.count
        )
    }

    private func loadLocalFixture(
        normalTabCount: Int,
        privateTabCount: Int,
        selectPrivate: Bool
    ) {
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
              body { font:17px -apple-system,sans-serif; margin:28px; line-height:1.45; min-height:180vh }
              h1 { color:#c2410c }
              button { background:#f97316; color:white; border:0; border-radius:10px; padding:12px }
              .fixture-actions { display:grid; gap:10px; max-width:340px; margin:20px 0 }
              .fixture-actions,input { max-width:100%; box-sizing:border-box }
              input[type="file"] { width:100% !important }
              label { display:block; font-weight:600; margin:18px 0 8px }
              #nested-scroll-fixture {
                display:none;
                height:100%;
                overflow-y:auto;
                overscroll-behavior:contain;
                box-sizing:border-box;
                padding:28px;
                background:Canvas;
              }
              #nested-scroll-content { min-height:240vh }
              body.nested-scroll-active {
                height:100vh;
                min-height:100vh;
                margin:0;
                overflow:hidden;
              }
              body.nested-scroll-active main { height:100% }
              body.nested-scroll-active main > :not(#nested-scroll-fixture) { display:none }
              body.nested-scroll-active #nested-scroll-fixture { display:block }
            </style></head><body>
            <main><h1>Ahoi fixture page</h1>
            <p>This page is provided locally for deterministic browser UI tests.</p>
            <p id="find-target">Ahoi visible find target</p>
            <button id="nested-scroll-activate" aria-label="Activate nested scroll fixture" onclick="activateNestedScrollFixture()">Activate nested scroll fixture</button>
            <section id="nested-scroll-fixture" role="region" aria-label="Nested scroll fixture" tabindex="0">
              <div id="nested-scroll-content">
                <h1>Nested scroll starts here</h1>
                <p>This independently scrolling region models modern application pages.</p>
                <p style="margin-top:180vh">Nested scroll end marker</p>
              </div>
            </section>
            <ul>
              <li><a href="https://example.com">Open HTTPS page</a></li>
              <li><a id="link-actions-fixture" href="https://example.com/ahoi-link-actions" aria-label="Open Ahoi link actions">Long-press for link actions</a></li>
              <li><a href="https://example.com/?ahoi-popup=1" target="_blank">Open target blank</a></li>
              <li><a href="https://httpbin.org/response-headers?Content-Disposition=attachment%3B%20filename%3Dahoi-fixture.txt&amp;Content-Type=text%2Fplain">Download fixture</a></li>
              <li><a aria-label="Open privacy-sensitive mail app" href="mailto:browser-test@example.com?subject=ahoi-secret-subject&amp;body=ahoi-secret-body">Open mail app</a></li>
            </ul>
            <section class="fixture-actions" aria-label="JavaScript dialog fixtures">
              <button id="js-alert" aria-label="Show JavaScript alert" onclick="alert('Ahoi alert fixture')">Show JavaScript alert</button>
              <button id="js-confirm" aria-label="Show JavaScript confirm" onclick="showFixtureConfirm()">Show JavaScript confirm</button>
              <button id="js-prompt" aria-label="Show JavaScript prompt" onclick="showFixturePrompt()">Show JavaScript prompt</button>
              <output id="dialog-result" aria-live="polite">No dialog result yet.</output>
            </section>
            <label for="upload">Choose a fixture file</label>
            <p><input id="upload" aria-label="Choose a fixture file" type="file" onchange="reportUpload(this.files)" style="font-size:20px;padding:12px;border:1px solid #777;border-radius:10px;width:340px"></p>
            <output id="upload-result" aria-label="File selection result" aria-live="polite">No file selected.</output>
            <section class="fixture-actions" aria-label="Website permission fixtures">
              <button id="camera" aria-label="Request camera permission" onclick="requestMedia('camera')">Request camera</button>
              <button id="microphone" aria-label="Request microphone permission" onclick="requestMedia('microphone')">Request microphone</button>
              <button id="camera-microphone" aria-label="Request camera and microphone permission" onclick="requestMedia('cameraAndMicrophone')">Request camera and microphone</button>
              <button id="motion" aria-label="Request motion permission" onclick="requestMotion()">Request motion</button>
              <output id="permission-result" aria-label="Website permission result" aria-live="polite">No permission result yet.</output>
            </section>
            </main>
            <script>
              const dialogResult = document.getElementById('dialog-result');
              function activateNestedScrollFixture() {
                document.body.classList.add('nested-scroll-active');
                const scroller = document.getElementById('nested-scroll-fixture');
                scroller.scrollTop = 0;
                scroller.focus();
              }
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
              const uploadResult = document.getElementById('upload-result');
              function reportUpload(files) {
                if (!files || files.length === 0) {
                  uploadResult.textContent = 'No file selected.';
                  return;
                }
                const file = files[0];
                uploadResult.textContent = 'Selected file: ' + file.name + ' · ' + file.size + ' bytes.';
              }
              const permissionResult = document.getElementById('permission-result');
              async function requestMedia(kind) {
                try {
                  const constraints = kind === 'camera'
                    ? {video:true}
                    : kind === 'microphone'
                      ? {audio:true}
                      : {video:true,audio:true};
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
        loadScaleTabs(
            baseURL: fixtureURL,
            normalCount: normalTabCount,
            privateCount: privateTabCount,
            selectPrivate: selectPrivate
        )
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

    /// Runs a bounded synthetic workload only in a DEBUG candidate. These
    /// launch-argument workloads make Instruments captures reproducible; they
    /// remain integration diagnostics and never count as visible E2E proof.
    public func runPerformanceWorkload(
        _ request: MobilePerformanceLaunchRequest,
        systemReduceMotion: Bool = UIAccessibility.isReduceMotionEnabled
    ) async {
        let workload = request.workload
        let effectiveReduceMotion = MobilePerformanceMotionPolicy.effectiveReduceMotion(
            requestedReduceMotion: request.reduceMotion,
            systemReduceMotion: systemReduceMotion
        )
        let evidence = MobilePerformanceWorkloadEvidence(
            request: request,
            effectiveReduceMotion: effectiveReduceMotion
        )
        var samples = MobilePerformanceEvidenceSamples()

        Self.performanceWorkloadLogger.info(
            "workload_begin kind=\(workload.rawValue, privacy: .public) requested_reduce_motion=\(request.reduceMotion, privacy: .public) system_reduce_motion=\(systemReduceMotion, privacy: .public) effective_reduce_motion=\(effectiveReduceMotion, privacy: .public)"
        )
        switch workload {
        case .idle:
            if request.scenario == "launch-cold" ||
                request.scenario == "launch-warm-cache",
               let milliseconds = performanceRecorder.snapshot.launchMilliseconds {
                samples.sessionReadyMilliseconds = [Double(milliseconds)]
            }
            try? await Task.sleep(for: .milliseconds(750))
        case .discardRestore:
            let mode: MobileBrowsingMode = request.privateTabCount > 0
                ? .privateBrowsing
                : .normal
            for _ in 0..<10 {
                let discardStart = DispatchTime.now().uptimeNanoseconds
                discardInactivePages(keeping: 2)
                samples.discardMilliseconds.append(
                    Self.elapsedPerformanceMilliseconds(since: discardStart)
                )
                let restoreStart = DispatchTime.now().uptimeNanoseconds
                await restorePerformanceFixturePage(mode: mode)
                samples.restoreMilliseconds.append(
                    Self.elapsedPerformanceMilliseconds(since: restoreStart)
                )
            }
        case .lifecycleFlush:
            for _ in 0..<10 {
                await restorePerformanceFixturePages(upTo: 5, mode: .normal)
                samples.livePagesForeground.append(Double(pages.count))
                let backgroundStart = DispatchTime.now().uptimeNanoseconds
                prepareForInactiveScene()
                discardInactivePages(keeping: 2)
                samples.backgroundPolicyMilliseconds.append(
                    Self.elapsedPerformanceMilliseconds(since: backgroundStart)
                )
                samples.livePagesBackground.append(Double(pages.count))
                let failuresBefore = performanceRecorder.snapshot.sessionFlushFailureCount
                await flushSession()
                if let milliseconds = performanceRecorder.snapshot
                    .latestSessionFlushMilliseconds {
                    samples.sessionFlushMilliseconds.append(Double(milliseconds))
                }
                samples.sessionFlushFailureCounts.append(Double(
                    performanceRecorder.snapshot.sessionFlushFailureCount - failuresBefore
                ))
                discardInactivePages(keeping: 1)
                samples.livePagesMemoryWarning.append(Double(pages.count))
                let foregroundStart = DispatchTime.now().uptimeNanoseconds
                await restorePerformanceFixturePages(upTo: 5, mode: .normal)
                samples.foregroundRestoreMilliseconds.append(
                    Self.elapsedPerformanceMilliseconds(since: foregroundStart)
                )
            }
        case .scroll:
            try? await Task.sleep(for: .milliseconds(300))
            let behavior = effectiveReduceMotion ? "auto" : "smooth"
            for _ in 0..<14 {
                _ = try? await selectedPage?.callJavaScript(
                    "window.scrollTo({top: document.documentElement.scrollHeight, behavior: behavior})",
                    arguments: ["behavior": behavior]
                )
                try? await Task.sleep(for: .milliseconds(700))
                _ = try? await selectedPage?.callJavaScript(
                    "window.scrollTo({top: 0, behavior: behavior})",
                    arguments: ["behavior": behavior]
                )
                try? await Task.sleep(for: .milliseconds(700))
            }
        }
        Self.performanceWorkloadLogger.info(
            "workload_end kind=\(workload.rawValue, privacy: .public) tabs=\(self.tabs.count, privacy: .public) live_pages=\(self.pages.count, privacy: .public)"
        )
        await evidence.writeCompleted(
            normalTabs: normalTabs.count,
            privateTabs: privateTabs.count,
            livePages: pages.count,
            samples: samples
        )
    }

    private func restorePerformanceFixturePages(
        upTo maximumLivePages: Int,
        mode: MobileBrowsingMode
    ) async {
        while pages.count < maximumLivePages {
            let before = pages.count
            await restorePerformanceFixturePage(mode: mode)
            if pages.count == before { return }
        }
    }

    private func restorePerformanceFixturePage(mode: MobileBrowsingMode) async {
        guard let tab = tabs.first(where: {
            $0.mode == mode && pages[$0.id] == nil
        }),
        let urlValue = tab.url,
        let url = URL(string: urlValue) else { return }
        let page = makePage(tabID: tab.id, mode: tab.mode)
        pages[tab.id] = page
        observeNavigations(of: page, tabID: tab.id)
        page.load(
            simulatedRequest: URLRequest(url: url),
            responseHTML: "<html><title>Ahoi Restore</title><body>Ahoi restored fixture</body></html>"
        )
        for _ in 0..<100 {
            if let state = try? await page.callJavaScript("document.readyState"),
               state as? String == "complete" {
                break
            }
            try? await Task.sleep(for: .milliseconds(10))
        }
        performanceRecorder.recordTabState(
            normalTabs: normalTabs.count,
            privateTabs: privateTabs.count,
            livePages: pages.count
        )
    }

    private static func elapsedPerformanceMilliseconds(since start: UInt64) -> Double {
        let end = DispatchTime.now().uptimeNanoseconds
        guard end >= start else { return 0 }
        return Double(end - start) / 1_000_000
    }

    private func loadScaleTabs(
        baseURL: URL,
        normalCount: Int,
        privateCount: Int,
        selectPrivate: Bool
    ) {
        for index in 1..<normalCount {
            makeScaleTab(index: index, mode: .normal, baseURL: baseURL)
        }
        for index in 0..<privateCount {
            makeScaleTab(index: index, mode: .privateBrowsing, baseURL: baseURL)
        }
        if privateCount > 0,
           selectPrivate,
           let privateID = privateTabs.last?.id {
            select(privateID)
        }
        discardInactivePages(keeping: 5)
    }

    private func makeScaleTab(
        index: Int,
        mode: MobileBrowsingMode,
        baseURL: URL
    ) {
        let tabID = createTab(mode: mode, select: false)
        guard let page = page(for: tabID, createIfBlank: true),
              let url = URL(string: "\(baseURL.absoluteString)/scale-\(mode.rawValue)-\(index)") else {
            return
        }
        page.load(
            simulatedRequest: URLRequest(url: url),
            responseHTML: "<html><title>Ahoi Scale \(index)</title><body>Scale tab</body></html>"
        )
        guard let tabIndex = tabs.firstIndex(where: { $0.id == tabID }) else { return }
        tabs[tabIndex].url = url.absoluteString
        tabs[tabIndex].title = "Ahoi Scale \(index)"
    }

    private func uiTestTabCount(
        after key: String,
        in arguments: [String],
        defaultValue: Int
    ) -> Int {
        guard let keyIndex = arguments.firstIndex(of: key),
              arguments.indices.contains(keyIndex + 1),
              let value = Int(arguments[keyIndex + 1]) else {
            return defaultValue
        }
        return min(20, max(0, value))
    }

    private static let performanceWorkloadLogger = Logger(
        subsystem: "app.ahoibrowser.AhoiBrowser",
        category: "MobilePerformanceWorkload"
    )
#endif
}

public enum MobilePerformanceWorkload: String, Equatable, Sendable {
    case idle
    case discardRestore = "discard-restore"
    case lifecycleFlush = "lifecycle-flush"
    case scroll
}

public enum MobilePerformanceLaunchValidation: Equatable, Sendable {
    case notRequested
    case valid(MobilePerformanceLaunchRequest)
    case invalid
}

public struct MobilePerformanceLaunchRequest: Equatable, Sendable {
    public let scenario: String
    public let nonce: String
    public let markerFilename: String
    public let workload: MobilePerformanceWorkload
    public let normalTabCount: Int
    public let privateTabCount: Int
    public let selectPrivate: Bool
    public let reduceMotion: Bool

    public static func validate(
        arguments: [String]
    ) -> MobilePerformanceLaunchValidation {
        let mentioned = arguments.contains { $0.hasPrefix("-AhoiPerformance") }
        guard mentioned else { return .notRequested }
        let performanceKeys: Set<String> = [
            "-AhoiPerformanceWorkload",
            "-AhoiPerformanceEvidenceScenario",
            "-AhoiPerformanceEvidenceNonce",
            "-AhoiPerformanceEvidenceMarker",
            "-AhoiPerformanceReduceMotionOverride",
        ]
        let fixtureKeys: Set<String> = [
            "-AhoiUITestFixture",
            "-AhoiUITestNormalTabCount",
            "-AhoiUITestPrivateTabCount",
            "-AhoiUITestSelectPrivate",
        ]
        let overrideKey = "-AhoiPerformanceReduceMotionOverride"
        let overrideIndices = arguments.indices.filter { arguments[$0] == overrideKey }
        guard overrideIndices.count <= 1 else { return .invalid }
        let reduceMotionOverride: Bool?
        if let index = overrideIndices.first {
            guard arguments.indices.contains(index + 1) else { return .invalid }
            switch arguments[index + 1] {
            case "true": reduceMotionOverride = true
            case "false": reduceMotionOverride = false
            default: return .invalid
            }
        } else {
            reduceMotionOverride = nil
        }
        guard arguments.filter({ $0.hasPrefix("-AhoiPerformance") })
            .allSatisfy(performanceKeys.contains),
              arguments.filter({ $0.hasPrefix("-AhoiUITest") })
            .allSatisfy(fixtureKeys.contains),
              occurrences(of: "-AhoiUITestFixture", in: arguments) == 1,
              occurrences(of: "-AhoiUITestSelectPrivate", in: arguments) <= 1,
              let workloadValue = singleValue(
                after: "-AhoiPerformanceWorkload",
                in: arguments
              ),
              let workload = MobilePerformanceWorkload(rawValue: workloadValue),
              let scenario = singleValue(
                after: "-AhoiPerformanceEvidenceScenario",
                in: arguments
              ),
              let nonce = singleValue(
                after: "-AhoiPerformanceEvidenceNonce",
                in: arguments
              ),
              let marker = singleValue(
                after: "-AhoiPerformanceEvidenceMarker",
                in: arguments
              ),
              isSafeToken(scenario, maximumLength: 96),
              isSafeToken(nonce, maximumLength: 180),
              isSafeToken(marker, maximumLength: 128),
              marker == "ahoi-performance-\(scenario).json",
              let normalCount = tabCount(
                after: "-AhoiUITestNormalTabCount",
                in: arguments,
                defaultValue: 1
              ),
              let privateCount = tabCount(
                after: "-AhoiUITestPrivateTabCount",
                in: arguments,
                defaultValue: 0
              ),
              let specification = specification(for: scenario),
              specification.workload == workload,
              specification.normalTabCount == normalCount,
              specification.privateTabCount == privateCount,
              specification.reduceMotion == (reduceMotionOverride ?? false),
              specification.requiresReduceMotionOverride ==
                (reduceMotionOverride != nil),
              specification.selectPrivate == arguments.contains(
                "-AhoiUITestSelectPrivate"
              ) else {
            return .invalid
        }
        return .valid(MobilePerformanceLaunchRequest(
            scenario: scenario,
            nonce: nonce,
            markerFilename: marker,
            workload: workload,
            normalTabCount: normalCount,
            privateTabCount: privateCount,
            selectPrivate: specification.selectPrivate,
            reduceMotion: specification.reduceMotion
        ))
    }

    private struct ScenarioSpecification {
        let workload: MobilePerformanceWorkload
        let normalTabCount: Int
        let privateTabCount: Int
        let selectPrivate: Bool
        let reduceMotion: Bool
        let requiresReduceMotionOverride: Bool
    }

    private static func specification(
        for scenario: String
    ) -> ScenarioSpecification? {
        switch scenario {
        case "launch-cold", "launch-warm-cache", "memory-normal-1",
             "idle-resources", "idle-network":
            return .init(
                workload: .idle,
                normalTabCount: 1,
                privateTabCount: 0,
                selectPrivate: false,
                reduceMotion: false,
                requiresReduceMotionOverride: false
            )
        case "memory-normal-5":
            return .init(
                workload: .idle,
                normalTabCount: 5,
                privateTabCount: 0,
                selectPrivate: false,
                reduceMotion: false,
                requiresReduceMotionOverride: false
            )
        case "memory-normal-20-discard-restore":
            return .init(
                workload: .discardRestore,
                normalTabCount: 20,
                privateTabCount: 0,
                selectPrivate: false,
                reduceMotion: false,
                requiresReduceMotionOverride: false
            )
        case "memory-private-1":
            return privateSpecification(count: 1, workload: .idle)
        case "memory-private-5":
            return privateSpecification(count: 5, workload: .idle)
        case "memory-private-20-discard-restore":
            return privateSpecification(count: 20, workload: .discardRestore)
        case "controller-pressure-policy":
            return .init(
                workload: .lifecycleFlush,
                normalTabCount: 20,
                privateTabCount: 0,
                selectPrivate: false,
                reduceMotion: false,
                requiresReduceMotionOverride: false
            )
        case "scroll-motion-standard", "scroll-motion-reduced":
            return .init(
                workload: .scroll,
                normalTabCount: 1,
                privateTabCount: 0,
                selectPrivate: false,
                reduceMotion: scenario == "scroll-motion-reduced",
                requiresReduceMotionOverride: true
            )
        default:
            return nil
        }
    }

    private static func privateSpecification(
        count: Int,
        workload: MobilePerformanceWorkload
    ) -> ScenarioSpecification {
        .init(
            workload: workload,
            normalTabCount: 1,
            privateTabCount: count,
            selectPrivate: true,
            reduceMotion: false,
            requiresReduceMotionOverride: false
        )
    }

    private static func singleValue(
        after key: String,
        in arguments: [String]
    ) -> String? {
        let indices = arguments.indices.filter { arguments[$0] == key }
        guard indices.count == 1,
              let index = indices.first,
              arguments.indices.contains(index + 1),
              !arguments[index + 1].hasPrefix("-") else {
            return nil
        }
        return arguments[index + 1]
    }

    private static func tabCount(
        after key: String,
        in arguments: [String],
        defaultValue: Int
    ) -> Int? {
        let indices = arguments.indices.filter { arguments[$0] == key }
        guard indices.count <= 1 else { return nil }
        guard let index = indices.first else { return defaultValue }
        guard arguments.indices.contains(index + 1),
              let value = Int(arguments[index + 1]),
              (0...20).contains(value) else {
            return nil
        }
        return value
    }

    private static func occurrences(
        of key: String,
        in arguments: [String]
    ) -> Int {
        arguments.lazy.filter { $0 == key }.count
    }

    private static func isSafeToken(_ value: String, maximumLength: Int) -> Bool {
        !value.isEmpty && value.count <= maximumLength && value.unicodeScalars.allSatisfy {
            CharacterSet.alphanumerics.contains($0) || "-_.".unicodeScalars.contains($0)
        }
    }
}
