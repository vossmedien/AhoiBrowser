import XCTest
@testable import AhoiMobileCore

final class MobilePerformanceIsolationTests: XCTestCase {
    func testSystemReduceMotionAlwaysWinsPerformanceOverride() {
        XCTAssertFalse(MobilePerformanceMotionPolicy.effectiveReduceMotion(
            requestedReduceMotion: false,
            systemReduceMotion: false
        ))
        XCTAssertTrue(MobilePerformanceMotionPolicy.effectiveReduceMotion(
            requestedReduceMotion: false,
            systemReduceMotion: true
        ))
        XCTAssertTrue(MobilePerformanceMotionPolicy.effectiveReduceMotion(
            requestedReduceMotion: true,
            systemReduceMotion: false
        ))
        XCTAssertTrue(MobilePerformanceMotionPolicy.effectiveReduceMotion(
            requestedReduceMotion: true,
            systemReduceMotion: true
        ))
    }

    func testMarkerBindsEffectiveSystemReduceMotionNotRequestedStandardMotion() throws {
        let request = try validRequest(
            scenario: "scroll-motion-standard",
            workload: .scroll,
            reduceMotionOverride: false
        )
        let payload = MobilePerformanceWorkloadEvidence(
            request: request,
            effectiveReduceMotion: true
        ).completedPayload(
            normalTabs: 1,
            privateTabs: 0,
            livePages: 1,
            samples: .init()
        )

        XCTAssertFalse(request.reduceMotion)
        XCTAssertTrue(payload.reduceMotion)
    }

    func testOrdinaryUITestLaunchDoesNotSelectPerformanceRuntime() {
        XCTAssertEqual(
            MobilePerformanceLaunchRequest.validate(
                arguments: ["AhoiMobile", "-AhoiUITestFixture"]
            ),
            .notRequested
        )
    }

    func testEveryCaptureScenarioProducesABoundRequest() throws {
        let cases: [(String, MobilePerformanceWorkload, Int, Int, Bool, Bool)] = [
            ("launch-cold", .idle, 1, 0, false, false),
            ("launch-warm-cache", .idle, 1, 0, false, false),
            ("memory-normal-1", .idle, 1, 0, false, false),
            ("memory-normal-5", .idle, 5, 0, false, false),
            ("memory-normal-20-discard-restore", .discardRestore, 20, 0, false, false),
            ("memory-private-1", .idle, 1, 1, true, false),
            ("memory-private-5", .idle, 1, 5, true, false),
            ("memory-private-20-discard-restore", .discardRestore, 1, 20, true, false),
            ("idle-resources", .idle, 1, 0, false, false),
            ("idle-network", .idle, 1, 0, false, false),
            ("controller-pressure-policy", .lifecycleFlush, 20, 0, false, false),
            ("scroll-motion-standard", .scroll, 1, 0, false, false),
            ("scroll-motion-reduced", .scroll, 1, 0, false, true),
        ]

        for item in cases {
            let validation = MobilePerformanceLaunchRequest.validate(
                arguments: arguments(
                    scenario: item.0,
                    workload: item.1,
                    normalTabs: item.2,
                    privateTabs: item.3,
                    selectPrivate: item.4,
                    reduceMotionOverride: item.1 == .scroll ? item.5 : nil
                )
            )
            guard case let .valid(request) = validation else {
                XCTFail("Expected a valid request for \(item.0)")
                continue
            }
            XCTAssertEqual(request.scenario, item.0)
            XCTAssertEqual(request.workload, item.1)
            XCTAssertEqual(request.normalTabCount, item.2)
            XCTAssertEqual(request.privateTabCount, item.3)
            XCTAssertEqual(request.selectPrivate, item.4)
            XCTAssertEqual(request.reduceMotion, item.5)
            XCTAssertEqual(
                request.markerFilename,
                "ahoi-performance-\(item.0).json"
            )
        }
    }

    func testAnyPerformanceFragmentFailsClosed() {
        let fragments = [
            ["AhoiMobile", "-AhoiPerformanceWorkload", "idle"],
            ["AhoiMobile", "-AhoiPerformanceEvidenceScenario", "idle-resources"],
            ["AhoiMobile", "-AhoiPerformanceEvidenceNonce", "nonce"],
            ["AhoiMobile", "-AhoiPerformanceEvidenceMarker", "marker.json"],
        ]
        for fragment in fragments {
            XCTAssertEqual(
                MobilePerformanceLaunchRequest.validate(arguments: fragment),
                .invalid
            )
        }
    }

    func testMalformedOrMismatchedPerformanceRequestsFailClosed() {
        let valid = arguments(
            scenario: "memory-normal-5",
            workload: .idle,
            normalTabs: 5
        )
        let malformed: [[String]] = [
            valid + ["-AhoiPerformanceUnknown", "value"],
            valid + ["-AhoiPerformanceWorkload", "idle"],
            replacingValue(after: "-AhoiPerformanceEvidenceNonce", in: valid, with: "bad/path"),
            replacingValue(
                after: "-AhoiPerformanceEvidenceMarker",
                in: valid,
                with: "ahoi-performance-other.json"
            ),
            replacingValue(after: "-AhoiUITestNormalTabCount", in: valid, with: "20"),
            replacingValue(
                after: "-AhoiPerformanceWorkload",
                in: valid,
                with: MobilePerformanceWorkload.scroll.rawValue
            ),
            valid + ["-AhoiUITestOffline"],
            valid + ["-AhoiPerformanceReduceMotionOverride", "false"],
            arguments(
                scenario: "scroll-motion-standard",
                workload: .scroll,
                reduceMotionOverride: nil
            ),
            arguments(
                scenario: "scroll-motion-reduced",
                workload: .scroll,
                reduceMotionOverride: false
            ),
        ]
        for arguments in malformed {
            XCTAssertEqual(
                MobilePerformanceLaunchRequest.validate(arguments: arguments),
                .invalid
            )
        }
    }

    @MainActor
    func testPerformanceFixtureDoesNotLoadPersistedSession() async throws {
        let persistedTab = MobileTabRecord(url: "https://persisted.example/secret")
        let store = CountingMobileSessionStore(snapshot: .init(
            tabs: [persistedTab],
            selectedTabID: persistedTab.id
        ))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiPerformanceIsolationTests-\(UUID())",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let browser = MobileBrowserController(
            store: store,
            downloadCoordinator: MobileDownloadCoordinator(directoryURL: directory)
        )
        let request = try validRequest(
            scenario: "memory-normal-5",
            workload: .idle,
            normalTabs: 5
        )

        browser.loadPerformanceFixture(request)

        let loadCount = await store.loadCount()
        XCTAssertEqual(loadCount, 0)
        XCTAssertEqual(browser.normalTabs.count, 5)
        XCTAssertTrue(browser.privateTabs.isEmpty)
        XCTAssertFalse(browser.tabs.contains { $0.url?.contains("persisted.example") == true })
        XCTAssertTrue(browser.tabs.allSatisfy {
            $0.url?.contains("fixture.ahoibrowser.test") == true
        })
    }

    @MainActor
    func testNormalControllerLoadStillRestoresItsSession() async throws {
        let persistedTab = MobileTabRecord(url: "https://persisted.example/normal")
        let store = CountingMobileSessionStore(snapshot: .init(
            tabs: [persistedTab],
            selectedTabID: persistedTab.id
        ))
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
            "AhoiNormalLoadIsolationTests-\(UUID())",
            isDirectory: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let browser = MobileBrowserController(
            store: store,
            downloadCoordinator: MobileDownloadCoordinator(directoryURL: directory)
        )

        await browser.load()

        let loadCount = await store.loadCount()
        XCTAssertEqual(loadCount, 1)
        XCTAssertEqual(browser.tabs.map(\.url), [persistedTab.url])
    }

    private func validRequest(
        scenario: String,
        workload: MobilePerformanceWorkload,
        normalTabs: Int = 1,
        privateTabs: Int = 0,
        selectPrivate: Bool = false,
        reduceMotionOverride: Bool? = nil
    ) throws -> MobilePerformanceLaunchRequest {
        let validation = MobilePerformanceLaunchRequest.validate(
            arguments: arguments(
                scenario: scenario,
                workload: workload,
                normalTabs: normalTabs,
                privateTabs: privateTabs,
                selectPrivate: selectPrivate,
                reduceMotionOverride: reduceMotionOverride
            )
        )
        guard case let .valid(request) = validation else {
            throw XCTSkip("Test setup did not produce a valid performance request.")
        }
        return request
    }

    private func arguments(
        scenario: String,
        workload: MobilePerformanceWorkload,
        normalTabs: Int = 1,
        privateTabs: Int = 0,
        selectPrivate: Bool = false,
        reduceMotionOverride: Bool? = nil
    ) -> [String] {
        var result = [
            "AhoiMobile",
            "-AhoiUITestFixture",
            "-AhoiPerformanceWorkload", workload.rawValue,
            "-AhoiPerformanceEvidenceScenario", scenario,
            "-AhoiPerformanceEvidenceNonce", "source-sha-1234-\(scenario)",
            "-AhoiPerformanceEvidenceMarker", "ahoi-performance-\(scenario).json",
        ]
        if normalTabs != 1 {
            result += ["-AhoiUITestNormalTabCount", "\(normalTabs)"]
        }
        if privateTabs > 0 {
            result += ["-AhoiUITestPrivateTabCount", "\(privateTabs)"]
        }
        if selectPrivate { result.append("-AhoiUITestSelectPrivate") }
        if let reduceMotionOverride {
            result += [
                "-AhoiPerformanceReduceMotionOverride",
                reduceMotionOverride ? "true" : "false",
            ]
        }
        return result
    }

    private func replacingValue(
        after key: String,
        in arguments: [String],
        with replacement: String
    ) -> [String] {
        var result = arguments
        guard let index = result.firstIndex(of: key),
              result.indices.contains(index + 1) else {
            return result
        }
        result[index + 1] = replacement
        return result
    }
}

private actor CountingMobileSessionStore: MobileBrowserSessionStoring {
    private let snapshot: MobileBrowserSessionSnapshot
    private var loads = 0

    init(snapshot: MobileBrowserSessionSnapshot) {
        self.snapshot = snapshot
    }

    func load() async throws -> MobileBrowserSessionSnapshot {
        loads += 1
        return snapshot
    }

    func save(_ snapshot: MobileBrowserSessionSnapshot) async throws {}

    func loadCount() -> Int {
        loads
    }
}
