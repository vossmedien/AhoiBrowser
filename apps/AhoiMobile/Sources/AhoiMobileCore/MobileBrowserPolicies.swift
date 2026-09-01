import CryptoKit
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
    struct TrackedRequest {
        let request: URLRequest
        let sourceOrigin: String
        let isMainFrame: Bool
    }

    private(set) var requestsAwaitingResponse: [TrackedRequest] = []

    mutating func record(
        _ request: URLRequest,
        sourceOrigin: String,
        isMainFrame: Bool
    ) {
        requestsAwaitingResponse.append(TrackedRequest(
            request: request,
            sourceOrigin: sourceOrigin,
            isMainFrame: isMainFrame
        ))
        if requestsAwaitingResponse.count > 32 {
            requestsAwaitingResponse.removeFirst(requestsAwaitingResponse.count - 32)
        }
    }

    mutating func take(matching responseURL: URL?) -> TrackedRequest? {
        guard let responseURL,
              let normalizedResponseURL = Self.networkURL(responseURL) else {
            return nil
        }
        let matchingIndices = requestsAwaitingResponse.indices.filter { index in
            requestsAwaitingResponse[index].request.url.flatMap(Self.networkURL)
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

enum MobileNavigationTargetPolicy {
    enum Decision: Equatable {
        case web(URL)
        case externalApp(URL)
        case blocked
    }

    private static let allowedExternalSchemes: Set<String> = [
        "facetime",
        "facetime-audio",
        "mailto",
        "sms",
        "tel",
    ]

    static func decide(_ url: URL) -> Decision {
        if let safeWebURL = try? MobileBrowserInputRouter.validateWebURL(url) {
            return .web(safeWebURL)
        }
        guard url.absoluteString.utf8.count <= MobileTabRecord.maximumURLUTF8Bytes,
              url.absoluteString.rangeOfCharacter(from: .controlCharacters) == nil,
              let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              let scheme = components.scheme?.lowercased(),
              allowedExternalSchemes.contains(scheme),
              components.user == nil,
              components.password == nil else {
            return .blocked
        }
        return .externalApp(url)
    }
}

enum MobileHTTPFailurePolicy {
    static func pageFailureKind(
        for statusCode: Int,
        isMainFrame: Bool
    ) -> MobilePageFailureKind? {
        // WebPage.NavigationResponse does not expose WKNavigationResponse's
        // main-frame bit. Only a response uniquely paired with a main-frame
        // NavigationAction may replace the whole browser surface.
        guard isMainFrame else { return nil }
        switch statusCode {
        case 400..<500:
            return .httpClientError
        case 500..<600:
            return .httpServerError
        default:
            return nil
        }
    }
}

@MainActor
final class MobileNavigationPolicyHandler: WebPage.NavigationDeciding {
    var onOpenNewTab: ((URL) -> Void)?
    var onExternalScheme: ((URL, String) -> Void)?
    var onBlockedNavigation: ((URL) -> Void)?
    var onHTTPFailure: ((URL, Int, MobilePageFailureKind) -> Void)?
    var onDownload: ((URLRequest, String) -> Void)?
    var onDownloadRejected: ((URL?, String?, MobileDownloadRejectionReason) -> Void)?
    private var requestTracker = MobileNavigationRequestTracker()

    func decidePolicy(
        for action: WebPage.NavigationAction,
        preferences: inout WebPage.NavigationPreferences
    ) async -> WKNavigationActionPolicy {
        guard let url = action.request.url else { return .cancel }
        let sourceOrigin = MobileBrowserOriginFormatter.label(
            for: action.source.securityOrigin,
            fallbackURL: action.source.request.url
        )

        if action.shouldPerformDownload {
            routeReplayableDownload(action.request, sourceOrigin: sourceOrigin)
            return .cancel
        }

        switch MobileNavigationTargetPolicy.decide(url) {
        case .blocked:
            onBlockedNavigation?(url)
            return .cancel
        case .externalApp(let externalURL):
            if action.navigationType == .linkActivated {
                onExternalScheme?(
                    externalURL,
                    MobileBrowserOriginFormatter.label(
                        for: action.source.securityOrigin,
                        fallbackURL: action.source.request.url
                    )
                )
            }
            return .cancel
        case .web(let safeURL):
            // A user-mediated fallback is useful only when the user actually
            // requested HTTP. Applying it to an explicit HTTPS URL replaces
            // transport/DNS failures with WebKit's downgrade interstitial and
            // prevents Ahoi from presenting its own recovery state.
            preferences.preferredHTTPSNavigationPolicy = safeURL.scheme == "http"
                ? .userMediatedFallbackToHTTP
                : .keepAsRequested
            if action.target == nil, action.navigationType == .linkActivated {
                onOpenNewTab?(safeURL)
                return .cancel
            }
            requestTracker.record(
                action.request,
                sourceOrigin: sourceOrigin,
                isMainFrame: action.target?.isMainFrame == true
            )
            return .allow
        }
    }

    func decidePolicy(
        for response: WebPage.NavigationResponse
    ) async -> WKNavigationResponsePolicy {
        let originalRequest = requestTracker.take(matching: response.response.url)
        let isAttachment = (response.response as? HTTPURLResponse)?
            .value(forHTTPHeaderField: "Content-Disposition")?
            .localizedCaseInsensitiveContains("attachment") == true
        if let httpResponse = response.response as? HTTPURLResponse,
           let responseURL = httpResponse.url,
           let failure = MobileHTTPFailurePolicy.pageFailureKind(
               for: httpResponse.statusCode,
               isMainFrame: originalRequest?.isMainFrame == true
           ) {
            onHTTPFailure?(responseURL, httpResponse.statusCode, failure)
            return .cancel
        }
        guard isAttachment || !response.canShowMimeType else { return .allow }

        guard let originalRequest else {
            onDownloadRejected?(response.response.url, nil, .unmatchedResponse)
            return .cancel
        }
        routeReplayableDownload(
            originalRequest.request,
            sourceOrigin: originalRequest.sourceOrigin
        )
        return .cancel
    }

    func decideAuthenticationChallengeDisposition(
        for challenge: URLAuthenticationChallenge
    ) async -> (URLSession.AuthChallengeDisposition, URLCredential?) {
        (.performDefaultHandling, nil)
    }

    private func routeReplayableDownload(
        _ request: URLRequest,
        sourceOrigin: String
    ) {
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
            onDownloadRejected?(request.url, sourceOrigin, .unsafeMethod(method))
            return
        }
        onDownload?(request, sourceOrigin)
    }
}

public struct MobileExternalOpenDeduplicator: Sendable {
    /// iOS can spend several seconds restoring a terminated WebKit process
    /// before the same handoff is delivered again. Keep the gate bounded, but
    /// cover the complete cold-activation journey observed on real simulator
    /// scheduling rather than only the time spent inside application code.
    public static let activationRedeliveryWindow: TimeInterval = 15

    public var interval: TimeInterval
    private var lastURL: URL?
    private var lastAcceptedAt: Date?
    private let receiptStore: (any MobileExternalOpenReceiptStoring)?

    public init(interval: TimeInterval = activationRedeliveryWindow) {
        self.interval = interval
        self.receiptStore = nil
    }

    public init(
        interval: TimeInterval = activationRedeliveryWindow,
        receiptURL: URL
    ) {
        self.interval = interval
        self.receiptStore = FileMobileExternalOpenReceiptStore(fileURL: receiptURL)
    }

    init(
        interval: TimeInterval = activationRedeliveryWindow,
        receiptStore: any MobileExternalOpenReceiptStoring
    ) {
        self.interval = interval
        self.receiptStore = receiptStore
    }

    public mutating func accepts(_ url: URL, now: Date = Date()) -> Bool {
        let fingerprint = Self.fingerprint(for: url)
        if lastURL == url, isInsideWindow(lastAcceptedAt, now: now) {
            return false
        }
        let persisted = receiptStore.flatMap { try? $0.load() }
        if let persisted,
           persisted.fingerprint == fingerprint,
           isInsideWindow(persisted.acceptedAt, now: now) {
            return false
        }
        // Rejected redeliveries do not extend the window. A bounded window is
        // long enough for a cold iOS scene activation, while still allowing a
        // deliberate later open of the same URL.
        lastURL = url
        lastAcceptedAt = now
        if let receiptStore {
            try? receiptStore.save(.init(
                fingerprint: fingerprint,
                acceptedAt: now
            ))
        }
        return true
    }

    mutating func rememberAccepted(_ url: URL, now: Date = Date()) {
        lastURL = url
        lastAcceptedAt = now
    }

    private func isInsideWindow(_ acceptedAt: Date?, now: Date) -> Bool {
        guard let acceptedAt else { return false }
        let elapsed = now.timeIntervalSince(acceptedAt)
        return elapsed >= 0 && elapsed <= interval
    }

    private static func fingerprint(for url: URL) -> String {
        Data(SHA256.hash(data: Data(url.absoluteString.utf8))).base64EncodedString()
    }
}

struct MobileExternalOpenReceipt: Codable, Equatable, Sendable {
    let fingerprint: String
    let acceptedAt: Date
}

protocol MobileExternalOpenReceiptStoring: Sendable {
    func load() throws -> MobileExternalOpenReceipt?
    func save(_ receipt: MobileExternalOpenReceipt) throws
}

struct FileMobileExternalOpenReceiptStore: MobileExternalOpenReceiptStoring {
    private static let maximumReceiptBytes = 4 * 1_024
    let fileURL: URL

    func load() throws -> MobileExternalOpenReceipt? {
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            return nil
        }
        let data = try Data(contentsOf: fileURL, options: .mappedIfSafe)
        guard data.count <= Self.maximumReceiptBytes else { return nil }
        return try JSONDecoder().decode(MobileExternalOpenReceipt.self, from: data)
    }

    func save(_ receipt: MobileExternalOpenReceipt) throws {
        try FileManager.default.createDirectory(
            at: fileURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        let data = try JSONEncoder().encode(receipt)
        guard data.count <= Self.maximumReceiptBytes else { return }
        try data.write(to: fileURL, options: .atomic)
    }
}
