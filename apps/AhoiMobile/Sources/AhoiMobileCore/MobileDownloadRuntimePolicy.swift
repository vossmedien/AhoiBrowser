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
