import Foundation
import WebKit

public enum MobileBrowserOriginFormatter {
    public static func label(for url: URL) -> String {
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              let scheme = components.scheme?.lowercased(),
              let host = components.host,
              !host.isEmpty else {
            return url.absoluteString
        }
        let port = components.port.map { ":\($0)" } ?? ""
        return "\(scheme)://\(host)\(port)"
    }

    @MainActor
    static func label(for origin: WKSecurityOrigin, fallbackURL: URL? = nil) -> String {
        let scheme = origin.protocol.lowercased()
        let host = origin.host
        guard !scheme.isEmpty, !host.isEmpty else {
            if let fallbackURL { return label(for: fallbackURL) }
            return CompanionL10n.string(
                "browser.origin.unknown",
                fallback: "Unknown origin"
            )
        }
        let port = origin.port > 0 ? ":\(origin.port)" : ""
        return "\(scheme)://\(host)\(port)"
    }
}

public struct MobilePermissionRequest: Identifiable, Equatable, Sendable {
    public enum Kind: String, Equatable, Sendable {
        case camera
        case microphone
        case cameraAndMicrophone
        case motion
    }

    public let id: UUID
    public let tabID: UUID
    public let origin: String
    public let kind: Kind

    public init(
        id: UUID = UUID(),
        tabID: UUID,
        origin: String,
        kind: Kind
    ) {
        self.id = id
        self.tabID = tabID
        self.origin = origin
        self.kind = kind
    }
}

extension MobilePermissionRequest.Kind {
    var localizedLabel: String {
        switch self {
        case .camera:
            return CompanionL10n.string("browser.permission.camera", fallback: "the camera")
        case .microphone:
            return CompanionL10n.string("browser.permission.microphone", fallback: "the microphone")
        case .cameraAndMicrophone:
            return CompanionL10n.string(
                "browser.permission.camera_microphone",
                fallback: "the camera and microphone"
            )
        case .motion:
            return CompanionL10n.string("browser.permission.motion", fallback: "motion sensors")
        }
    }
}

@MainActor
public final class MobilePermissionCoordinator: ObservableObject {
    @Published public private(set) var pendingRequest: MobilePermissionRequest?
    private var continuation: CheckedContinuation<WKPermissionDecision, Never>?

    public init() {}

    public func request(
        permission: WebPage.DeviceSensorAuthorization.Permission,
        origin: WKSecurityOrigin,
        tabID: UUID
    ) async -> WKPermissionDecision {
        resolve(.deny)
        guard let kind = Self.kind(permission) else { return .deny }
        let request = MobilePermissionRequest(
            tabID: tabID,
            origin: MobileBrowserOriginFormatter.label(for: origin),
            kind: kind
        )
        pendingRequest = request
        return await withTaskCancellationHandler {
            await withCheckedContinuation { continuation in
                self.continuation = continuation
            }
        } onCancel: {
            Task { @MainActor [weak self] in
                self?.cancel(requestID: request.id)
            }
        }
    }

    public func allow(requestID: UUID) {
        guard pendingRequest?.id == requestID else { return }
        resolve(.grant)
    }

    public func deny(requestID: UUID) {
        guard pendingRequest?.id == requestID else { return }
        resolve(.deny)
    }

    public func cancelPending(forTabID tabID: UUID) {
        guard pendingRequest?.tabID == tabID else { return }
        resolve(.deny)
    }

    public func cancelPending(unlessTabID tabID: UUID?) {
        guard let pendingRequest, pendingRequest.tabID != tabID else { return }
        resolve(.deny)
    }

    private func cancel(requestID: UUID) {
        guard pendingRequest?.id == requestID else { return }
        resolve(.deny)
    }

    private func resolve(_ decision: WKPermissionDecision) {
        pendingRequest = nil
        continuation?.resume(returning: decision)
        continuation = nil
    }

    private static func kind(
        _ permission: WebPage.DeviceSensorAuthorization.Permission
    ) -> MobilePermissionRequest.Kind? {
        switch permission {
        case .deviceOrientationAndMotion:
            return .motion
        case .mediaCapture(.camera):
            return .camera
        case .mediaCapture(.microphone):
            return .microphone
        case .mediaCapture(.cameraAndMicrophone):
            return .cameraAndMicrophone
        @unknown default:
            return nil
        }
    }

}

struct MobileNavigationRequestTracker {
    private(set) var requestsAwaitingResponse: [URLRequest] = []

    mutating func record(_ request: URLRequest) {
        requestsAwaitingResponse.append(request)
        if requestsAwaitingResponse.count > 32 {
            requestsAwaitingResponse.removeFirst(requestsAwaitingResponse.count - 32)
        }
    }

    mutating func take(matching responseURL: URL?) -> URLRequest? {
        guard let responseURL,
              let normalizedResponseURL = Self.networkURL(responseURL) else {
            return nil
        }
        let matchingIndices = requestsAwaitingResponse.indices.filter { index in
            requestsAwaitingResponse[index].url.flatMap(Self.networkURL)
                == normalizedResponseURL
        }
        guard matchingIndices.count == 1, let index = matchingIndices.first else {
            // Responses do not carry a navigation identity. If two requests
            // share a URL, choosing either could replay the wrong body.
            for index in matchingIndices.reversed() {
                requestsAwaitingResponse.remove(at: index)
            }
            return nil
        }
        return requestsAwaitingResponse.remove(at: index)
    }

    private static func networkURL(_ url: URL) -> URL? {
        guard var components = URLComponents(
            url: url,
            resolvingAgainstBaseURL: false
        ) else { return nil }
        components.fragment = nil
        return components.url
    }
}

enum MobileDownloadRejectionReason: Equatable, Sendable {
    case unsafeMethod(String)
    case unmatchedResponse
}

@MainActor
final class MobileNavigationPolicyHandler: WebPage.NavigationDeciding {
    var onOpenNewTab: ((URL) -> Void)?
    var onExternalScheme: ((URL, String) -> Void)?
    var onDownload: ((URLRequest) -> Void)?
    var onDownloadRejected: ((URL?, MobileDownloadRejectionReason) -> Void)?
    private var requestTracker = MobileNavigationRequestTracker()

    func decidePolicy(
        for action: WebPage.NavigationAction,
        preferences: inout WebPage.NavigationPreferences
    ) async -> WKNavigationActionPolicy {
        guard let url = action.request.url else { return .cancel }

        if action.shouldPerformDownload {
            routeReplayableDownload(action.request)
            return .cancel
        }

        let scheme = url.scheme?.lowercased()
        guard scheme == "http" || scheme == "https" else {
            if action.navigationType == .linkActivated {
                onExternalScheme?(
                    url,
                    MobileBrowserOriginFormatter.label(
                        for: action.source.securityOrigin,
                        fallbackURL: action.source.request.url
                    )
                )
            }
            return .cancel
        }

        preferences.preferredHTTPSNavigationPolicy = .userMediatedFallbackToHTTP
        if action.target == nil, action.navigationType == .linkActivated {
            onOpenNewTab?(url)
            return .cancel
        }
        requestTracker.record(action.request)
        return .allow
    }

    func decidePolicy(
        for response: WebPage.NavigationResponse
    ) async -> WKNavigationResponsePolicy {
        let originalRequest = requestTracker.take(matching: response.response.url)
        let isAttachment = (response.response as? HTTPURLResponse)?
            .value(forHTTPHeaderField: "Content-Disposition")?
            .localizedCaseInsensitiveContains("attachment") == true
        guard isAttachment || !response.canShowMimeType else { return .allow }

        guard let originalRequest else {
            onDownloadRejected?(response.response.url, .unmatchedResponse)
            return .cancel
        }
        routeReplayableDownload(originalRequest)
        return .cancel
    }

    func decideAuthenticationChallengeDisposition(
        for challenge: URLAuthenticationChallenge
    ) async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        (.performDefaultHandling, nil)
    }

    private func routeReplayableDownload(_ request: URLRequest) {
        // WebPage.NavigationDeciding can return `.download`, but WebKit for
        // SwiftUI does not expose the delegate callback that hands its
        // resulting WKDownload to the client. Until it does, only methods
        // defined as safe to repeat may use the explicit download WebView.
        let declaredMethod = (request.httpMethod ?? "GET")
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        let method = declaredMethod.isEmpty ? "GET" : declaredMethod
        let isSafeMethod = method == "GET" || method == "HEAD"
        guard isSafeMethod,
              request.httpBody == nil,
              request.httpBodyStream == nil else {
            onDownloadRejected?(request.url, .unsafeMethod(method))
            return
        }
        onDownload?(request)
    }
}

public enum MobileDownloadStatus: String, Codable, Sendable {
    case starting
    case downloading
    case completed
    case failed
    case cancelled
}

public struct MobileDownloadRecord: Identifiable, Equatable, Sendable {
    public let id: UUID
    public let sourceURL: URL
    public let sourceOrigin: String
    public var suggestedFilename: String
    public var destinationURL: URL?
    public var status: MobileDownloadStatus
    public var errorMessage: String?
    public var bytesReceived: Int64
    public var totalBytesExpected: Int64?
    public var progressFraction: Double?
    public let isPrivate: Bool

    public var progressPercent: Int? {
        guard let progressFraction, progressFraction.isFinite else { return nil }
        return Int((min(max(progressFraction, 0), 1) * 100).rounded())
    }

    public init(
        id: UUID = UUID(),
        sourceURL: URL,
        sourceOrigin: String? = nil,
        suggestedFilename: String,
        destinationURL: URL? = nil,
        status: MobileDownloadStatus = .starting,
        errorMessage: String? = nil,
        bytesReceived: Int64 = 0,
        totalBytesExpected: Int64? = nil,
        progressFraction: Double? = nil,
        isPrivate: Bool
    ) {
        self.id = id
        self.sourceURL = sourceURL
        self.sourceOrigin = sourceOrigin ?? MobileBrowserOriginFormatter.label(for: sourceURL)
        self.suggestedFilename = suggestedFilename
        self.destinationURL = destinationURL
        self.status = status
        self.errorMessage = errorMessage
        self.bytesReceived = max(bytesReceived, 0)
        self.totalBytesExpected = totalBytesExpected.flatMap { $0 >= 0 ? $0 : nil }
        self.progressFraction = progressFraction.flatMap { fraction in
            fraction.isFinite ? min(max(fraction, 0), 1) : nil
        }
        self.isPrivate = isPrivate
    }
}

@MainActor
public final class MobileDownloadCoordinator: NSObject, ObservableObject, WKDownloadDelegate {
    @Published public private(set) var downloads: [MobileDownloadRecord] = []

    private let directoryURL: URL
    private var webViews: [UUID: WKWebView] = [:]
    private var activeDownloads: [UUID: WKDownload] = [:]
    private var recordIDByDownload: [ObjectIdentifier: UUID] = [:]
    private var progressObservations: [UUID: NSKeyValueObservation] = [:]

    public init(directoryURL: URL) {
        self.directoryURL = directoryURL
        super.init()
    }

    public convenience override init() {
        let documents = FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask
        )[0]
        self.init(directoryURL: documents.appendingPathComponent("Downloads", isDirectory: true))
    }

    public func start(
        request: URLRequest,
        websiteDataStore: WKWebsiteDataStore,
        initiatingOrigin: String? = nil,
        isPrivate: Bool
    ) {
        guard let url = request.url,
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else { return }
        let id = UUID()
        downloads.insert(MobileDownloadRecord(
            id: id,
            sourceURL: url,
            sourceOrigin: initiatingOrigin,
            suggestedFilename: Self.safeFilename(url.lastPathComponent, fallback: "download"),
            isPrivate: isPrivate
        ), at: 0)

        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = websiteDataStore
        let webView = WKWebView(frame: .zero, configuration: configuration)
        webViews[id] = webView
        webView.startDownload(using: request) { [weak self] download in
            guard let self else { return }
            self.activeDownloads[id] = download
            self.recordIDByDownload[ObjectIdentifier(download)] = id
            download.delegate = self
            self.update(id) { $0.status = .downloading }
            self.observeProgress(of: download, id: id)
        }
    }

    public func recordFailure(
        sourceURL: URL,
        initiatingOrigin: String? = nil,
        isPrivate: Bool,
        message: String
    ) {
        guard (try? MobileBrowserInputRouter.validateWebURL(sourceURL)) != nil else {
            return
        }
        downloads.insert(MobileDownloadRecord(
            sourceURL: sourceURL,
            sourceOrigin: initiatingOrigin,
            suggestedFilename: Self.safeFilename(
                sourceURL.lastPathComponent,
                fallback: "download"
            ),
            status: .failed,
            errorMessage: message,
            isPrivate: isPrivate
        ), at: 0)
    }

    public func cancel(_ id: UUID) {
        guard let download = activeDownloads[id] else { return }
        download.cancel { [weak self] _ in
            Task { @MainActor in
                self?.update(id) { $0.status = .cancelled }
                self?.finish(id: id, download: download)
            }
        }
    }

    public func removeFinished() {
        downloads.removeAll { [.completed, .failed, .cancelled].contains($0.status) }
    }

    public func removeFinished(isPrivate: Bool) {
        downloads.removeAll {
            $0.isPrivate == isPrivate &&
                [.completed, .failed, .cancelled].contains($0.status)
        }
    }

    public func download(
        _ download: WKDownload,
        decideDestinationUsing response: URLResponse,
        suggestedFilename: String
    ) async -> URL? {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return nil }
        do {
            try FileManager.default.createDirectory(
                at: directoryURL,
                withIntermediateDirectories: true
            )
            let safeName = Self.safeFilename(suggestedFilename, fallback: "download")
            let destination = uniqueDestination(for: safeName)
            update(id) {
                $0.suggestedFilename = destination.lastPathComponent
                $0.destinationURL = destination
                if response.expectedContentLength >= 0 {
                    $0.totalBytesExpected = response.expectedContentLength
                }
            }
            return destination
        } catch {
            update(id) {
                $0.status = .failed
                $0.errorMessage = error.localizedDescription
            }
            return nil
        }
    }

    public func downloadDidFinish(_ download: WKDownload) {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return }
        update(id) {
            $0.status = .completed
            if let totalBytesExpected = $0.totalBytesExpected {
                $0.bytesReceived = max($0.bytesReceived, totalBytesExpected)
            }
            $0.progressFraction = 1
        }
        finish(id: id, download: download)
    }

    public func download(
        _ download: WKDownload,
        didFailWithError error: any Error,
        resumeData: Data?
    ) {
        guard let id = recordIDByDownload[ObjectIdentifier(download)] else { return }
        update(id) {
            $0.status = .failed
            $0.errorMessage = error.localizedDescription
        }
        finish(id: id, download: download)
    }

    private func update(_ id: UUID, mutation: (inout MobileDownloadRecord) -> Void) {
        guard let index = downloads.firstIndex(where: { $0.id == id }) else { return }
        mutation(&downloads[index])
    }

    private func observeProgress(of download: WKDownload, id: UUID) {
        let observation = download.progress.observe(
            \.fractionCompleted,
            options: [.initial, .new]
        ) { [weak self] progress, _ in
            let completedUnitCount = progress.completedUnitCount
            let totalUnitCount = progress.totalUnitCount
            let fractionCompleted = progress.isIndeterminate || totalUnitCount <= 0
                ? nil
                : progress.fractionCompleted
            Task { @MainActor [weak self] in
                self?.updateProgress(
                    id: id,
                    completedUnitCount: completedUnitCount,
                    totalUnitCount: totalUnitCount,
                    fractionCompleted: fractionCompleted
                )
            }
        }
        progressObservations[id] = observation
    }

    private func updateProgress(
        id: UUID,
        completedUnitCount: Int64,
        totalUnitCount: Int64,
        fractionCompleted: Double?
    ) {
        update(id) {
            guard $0.status == .starting || $0.status == .downloading else { return }
            $0.bytesReceived = max(completedUnitCount, 0)
            if totalUnitCount > 0 {
                $0.totalBytesExpected = totalUnitCount
            }
            $0.progressFraction = fractionCompleted.flatMap { fraction in
                fraction.isFinite ? min(max(fraction, 0), 1) : nil
            }
        }
    }

    private func finish(id: UUID, download: WKDownload) {
        progressObservations.removeValue(forKey: id)?.invalidate()
        activeDownloads.removeValue(forKey: id)
        webViews.removeValue(forKey: id)
        recordIDByDownload.removeValue(forKey: ObjectIdentifier(download))
    }

    private func uniqueDestination(for filename: String) -> URL {
        let source = URL(fileURLWithPath: filename)
        let stem = source.deletingPathExtension().lastPathComponent
        let ext = source.pathExtension
        var candidate = directoryURL.appendingPathComponent(filename)
        var suffix = 2
        while FileManager.default.fileExists(atPath: candidate.path) {
            let nextName = ext.isEmpty ? "\(stem)-\(suffix)" : "\(stem)-\(suffix).\(ext)"
            candidate = directoryURL.appendingPathComponent(nextName)
            suffix += 1
        }
        return candidate
    }

    nonisolated public static func safeFilename(_ value: String, fallback: String) -> String {
        let normalized = value.replacingOccurrences(of: "\\", with: "/")
        let rawLeaf = normalized
            .split(separator: "/", omittingEmptySubsequences: true)
            .last
            .map(String.init) ?? ""
        let leaf = rawLeaf
            .replacingOccurrences(of: ":", with: "-")
            .unicodeScalars
            .filter { !CharacterSet.controlCharacters.contains($0) }
            .map(String.init)
            .joined()
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !leaf.isEmpty, leaf != ".", leaf != ".." else { return fallback }
        return String(leaf.prefix(180))
    }
}

public struct MobileExternalOpenDeduplicator: Sendable {
    public static let activationRedeliveryWindow: TimeInterval = 8

    public var interval: TimeInterval
    private var lastURL: URL?
    private var lastAcceptedAt: Date?

    public init(interval: TimeInterval = activationRedeliveryWindow) {
        self.interval = interval
    }

    public mutating func accepts(_ url: URL, now: Date = Date()) -> Bool {
        if lastURL == url,
           let lastAcceptedAt,
           now.timeIntervalSince(lastAcceptedAt) <= interval {
            return false
        }
        // Rejected redeliveries do not extend the window. A bounded window is
        // long enough for a cold iOS scene activation, while still allowing a
        // deliberate later open of the same URL.
        lastURL = url
        lastAcceptedAt = now
        return true
    }
}
