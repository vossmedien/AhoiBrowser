import Foundation

#if DEBUG
struct MobilePerformanceEvidenceSamples: Codable, Equatable, Sendable {
    var sessionReadyMilliseconds: [Double] = []
    var sessionFlushMilliseconds: [Double] = []
    var sessionFlushFailureCounts: [Double] = []
    var livePagesForeground: [Double] = []
    var livePagesBackground: [Double] = []
    var livePagesMemoryWarning: [Double] = []
    var discardMilliseconds: [Double] = []
    var restoreMilliseconds: [Double] = []
    var backgroundPolicyMilliseconds: [Double] = []
    var foregroundRestoreMilliseconds: [Double] = []
}

struct MobilePerformanceMarkerPayload: Codable, Equatable, Sendable {
    let schemaVersion: Int
    let scenario: String
    let nonce: String
    let workload: String
    let status: String
    let reduceMotion: Bool
    let normalTabCount: Int
    let privateTabCount: Int
    let livePageCount: Int
    let samples: MobilePerformanceEvidenceSamples
}

enum MobilePerformanceMotionPolicy {
    static func effectiveReduceMotion(
        requestedReduceMotion: Bool,
        systemReduceMotion: Bool
    ) -> Bool {
        requestedReduceMotion || systemReduceMotion
    }
}

struct MobilePerformanceWorkloadEvidence: Sendable {
    let request: MobilePerformanceLaunchRequest
    let effectiveReduceMotion: Bool

    func completedPayload(
        normalTabs: Int,
        privateTabs: Int,
        livePages: Int,
        samples: MobilePerformanceEvidenceSamples
    ) -> MobilePerformanceMarkerPayload {
        MobilePerformanceMarkerPayload(
            schemaVersion: 2,
            scenario: request.scenario,
            nonce: request.nonce,
            workload: request.workload.rawValue,
            status: "completed",
            reduceMotion: effectiveReduceMotion,
            normalTabCount: min(max(normalTabs, 0), 20),
            privateTabCount: min(max(privateTabs, 0), 20),
            livePageCount: min(max(livePages, 0), 20),
            samples: samples
        )
    }

    func writeCompleted(
        normalTabs: Int,
        privateTabs: Int,
        livePages: Int,
        samples: MobilePerformanceEvidenceSamples
    ) async {
        let payload = completedPayload(
            normalTabs: normalTabs,
            privateTabs: privateTabs,
            livePages: livePages,
            samples: samples
        )
        let filename = request.markerFilename
        await Task.detached(priority: .utility) {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.sortedKeys]
            guard let data = try? encoder.encode(payload) else { return }
            let destination = FileManager.default.temporaryDirectory
                .appendingPathComponent(filename, isDirectory: false)
            try? data.write(to: destination, options: [.atomic])
        }.value
    }
}
#endif
