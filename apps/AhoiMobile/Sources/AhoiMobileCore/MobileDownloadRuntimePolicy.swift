import Foundation
import WebKit

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

extension MobileDownloadCoordinator {
    nonisolated static func isSameProcessRetryEligible(
        _ request: URLRequest,
        isPrivate: Bool
    ) -> Bool {
        guard !isPrivate,
              let url = request.url,
              (try? MobileBrowserInputRouter.validateWebURL(url)) != nil else {
            return false
        }
        let declaredMethod = (request.httpMethod ?? "GET")
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .uppercased()
        let method = declaredMethod.isEmpty ? "GET" : declaredMethod
        return (method == "GET" || method == "HEAD") &&
            request.httpBody == nil && request.httpBodyStream == nil
    }
}

enum MobileDownloadCookiePolicy {
    @MainActor
    static func prepare(
        _ request: URLRequest,
        websiteDataStore: WKWebsiteDataStore,
        initiatingOrigin: String?,
        completion: @escaping @MainActor (URLRequest) -> Void
    ) {
        guard request.value(forHTTPHeaderField: "Cookie") == nil,
              let url = request.url,
              isSameOrigin(initiatingOrigin, as: url) else {
            completion(request)
            return
        }
        websiteDataStore.httpCookieStore.getAllCookies { cookies in
            completion(addingApplicableCookies(cookies, to: request))
        }
    }

    static func addingApplicableCookies(
        _ cookies: [HTTPCookie],
        to request: URLRequest,
        now: Date = Date()
    ) -> URLRequest {
        guard request.value(forHTTPHeaderField: "Cookie") == nil,
              let url = request.url else { return request }
        let applicable = cookies.filter { applies($0, to: url, now: now) }
        guard !applicable.isEmpty,
              let header = HTTPCookie.requestHeaderFields(with: applicable)["Cookie"] else {
            return request
        }
        var prepared = request
        prepared.setValue(header, forHTTPHeaderField: "Cookie")
        return prepared
    }

    static func isSameOrigin(_ origin: String?, as url: URL) -> Bool {
        guard let origin,
              let source = URL(string: origin),
              source.scheme?.lowercased() == url.scheme?.lowercased(),
              source.host?.lowercased() == url.host?.lowercased() else {
            return false
        }
        return effectivePort(source) == effectivePort(url)
    }

    private static func applies(_ cookie: HTTPCookie, to url: URL, now: Date) -> Bool {
        guard let host = url.host?.lowercased(),
              let scheme = url.scheme?.lowercased(),
              cookie.expiresDate.map({ $0 > now }) ?? true,
              !cookie.isSecure || scheme == "https",
              matchesDomain(cookie.domain.lowercased(), host: host),
              matchesPath(cookie.path, requestPath: url.path.isEmpty ? "/" : url.path) else {
            return false
        }
        guard let ports = cookie.portList, !ports.isEmpty else { return true }
        guard let port = effectivePort(url) else { return false }
        return ports.contains { $0.intValue == port }
    }

    private static func matchesDomain(_ cookieDomain: String, host: String) -> Bool {
        guard cookieDomain.hasPrefix(".") else { return host == cookieDomain }
        let domain = String(cookieDomain.dropFirst())
        return host == domain || host.hasSuffix(".\(domain)")
    }

    private static func matchesPath(_ cookiePath: String, requestPath: String) -> Bool {
        let path = cookiePath.isEmpty ? "/" : cookiePath
        guard requestPath.hasPrefix(path) else { return false }
        guard requestPath.count > path.count, !path.hasSuffix("/") else { return true }
        return requestPath[requestPath.index(requestPath.startIndex, offsetBy: path.count)] == "/"
    }

    private static func effectivePort(_ url: URL) -> Int? {
        if let port = url.port { return port }
        switch url.scheme?.lowercased() {
        case "http": return 80
        case "https": return 443
        default: return nil
        }
    }
}

struct MobileDownloadRetryContext {
    let request: URLRequest
    let websiteDataStore: WKWebsiteDataStore
    let initiatingOrigin: String?

    init?(
        request: URLRequest,
        websiteDataStore: WKWebsiteDataStore,
        initiatingOrigin: String?,
        isPrivate: Bool
    ) {
        guard MobileDownloadCoordinator.isSameProcessRetryEligible(
            request,
            isPrivate: isPrivate
        ) else {
            return nil
        }
        self.request = request
        self.websiteDataStore = websiteDataStore
        self.initiatingOrigin = initiatingOrigin
    }
}

enum MobileDownloadRuntimeUpdate: Equatable, Sendable {
    case destination(
        filename: String,
        url: URL,
        expectedContentLength: Int64
    )
    case finished
    case failed(message: String)
}

extension MobileDownloadRecord {
    /// WebKit may deliver destination or terminal callbacks after cancellation.
    /// Runtime updates are monotonic: only an active record may advance, and a
    /// terminal user-visible state can never be replaced by a later callback.
    @discardableResult
    mutating func applyRuntimeUpdate(_ update: MobileDownloadRuntimeUpdate) -> Bool {
        guard status == .starting || status == .downloading else { return false }
        switch update {
        case let .destination(filename, url, expectedContentLength):
            suggestedFilename = filename
            destinationURL = url
            if expectedContentLength >= 0 {
                totalBytesExpected = expectedContentLength
            }
        case .finished:
            guard destinationURL != nil else {
                status = .failed
                errorMessage = MobileDownloadFailurePresentation.message(for: .destination)
                progressFraction = nil
                return true
            }
            status = .completed
            if let totalBytesExpected {
                bytesReceived = max(bytesReceived, totalBytesExpected)
            }
            progressFraction = 1
        case let .failed(message):
            status = .failed
            errorMessage = message
        }
        return true
    }
}
